#include "psvs.h"
#include "common.h"
#include "PSConstants.h"

/// this can be used to disable most of the shaders (they return just this color)
#if 0
#define RETURN_SIMPLE_COLOR oColor = half4(1,0,0,1); return;
#else
#define RETURN_SIMPLE_COLOR
#endif 

// ATOC fix (see news:iju89v$f8$1@new-server.localdomain)
#define ARMA3_ATOC_FIX 1
// clipping value for alpha for ATOC
#define ATOC_ALPHA_CLIP 0.5


#ifdef _XBOX
# define OPT_INPUTS [removeUnusedInputs]
#else
# define OPT_INPUTS
#endif

//! Samplers used by all the PS's
// DX11 - we have to setup both sampler register (s) and texture register (t)
// because they are separate - otherwise compiler could place textures to different slots
sampler2D samplers[16]            : register(s0) : register(t0);
sampler3D samplers3D[16]          : register(s0) : register(t0);
#define sampler0              samplers[0]
#define sampler1              samplers[1]
#define sampler2              samplers[2]
#define sampler3              samplers[3]
#define sampler4              samplers[4]
#define sampler5              samplers[5]
#define sampler6              samplers[6]
#define sampler7              samplers[7]
#define sampler8              samplers[8]
#define sampler9              samplers[9]
#define sampler10             samplers[10]
#define sampler11             samplers[11]
#define sampler12             samplers[12]
#define sampler13             samplers[13]
#define sampler14             samplers[14]
#define samplerTI             samplers[14]
#define samplerSN             samplers[14]
#define samplerShadowMap      samplers[15]
#define samplerTc             samplers[15]
#define samplerLightIntensity samplers[15]

//sampler2D samplerShadowMap        : register(s15); // samplerShadowMap must be performed via register (not an array), otherwise the X360 compilation will fail - the ASM in shadow buffers cannot read an array
//sampler2D samplerTc               : register(s15);
//sampler2D samplerLightIntensity   : register(s15);
#define samplerLightIntensity   sampler12

// ====================================
//! other constants
// ====================================

void DoAlphaTest(float alpha)
{
  // Alpha test replacement for DX11
  // DX11 TODO - this just a temporary solution - it is too slow! Make special shaders for alpha-tested materials
  if (PSC_AlphaTest.x > 0.0f)
  {
    if (alpha < PSC_AlphaTest.y)
      discard;
  }
}

//! An alternative to tex2D method, includes PSC_MaxColor multiplication
half4 tex2D0(half2 t)
{
  return tex2D(sampler0, t) * PSC_MaxColor;
}

//! An alternative to tex2D method, includes PSC_MaxColor multiplication and uses the mip level 0 only
half4 tex2DLOD0(half2 t)
{
#if _SM2
  half4 texCoord = half4(t.x, t.y, 0, -20);
  return tex2Dbias(sampler0, texCoord) * PSC_MaxColor;
#else
  half4 texCoord = half4(t.x, t.y, 0, 0);
  return tex2Dlod(sampler0, texCoord) * PSC_MaxColor;
#endif
}

//! Structure to hold separated components of light - GetLightX functions returns this
struct Light
{
  //! Light from the environment - roughly Ambient, DForced and Emmisive
  half3 indirect;
  //! Light from the main light source (light that disappears in a shadow) - roughly the diffuse light
  half3 direct;
  //! Specular light
  half3 specular;
  //! Specular light reflected from the environment (shadows doesn't affect it)
  half3 specularEnv;
  //! Diffuse shadow value, used where no ordinary shadows are available (f.i. for objects in distance)
  half diffuseShadowCoef;
};

/////////////////////////////////////////////////////////////////////////////////////////////
// Functions to calculate the light components

Light GetLightSimple(half3 ambient, half3 diffuse, half3 specular)
{
  Light light;
  light.indirect = ambient;
  light.direct = diffuse;
  light.specular = specular;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

#if 1

// coef - value from 0 to 1
half3 GetIndirectLightLerp(half coef)
{
  // middle color (= much darker hemispheric color)
  half3 middleCol = PSC_AE.rgb * 0.15f;

  // convert coef to -1, 1 range
  coef = 2*coef - 1;

  // first color (on the top)
  half3 col1 = lerp(middleCol, PSC_AE.rgb, saturate(coef));

  // second collor (on the bottom)
  half3 col2 = lerp(PSC_GE.rgb, middleCol, saturate(coef + 1));

  // select which color to use
  return lerp(col2, col1, saturate(sign(coef)));
}


// nDotU - value from -1 to 1
half3 GetIndirectLight(half nDotU)
{
  return GetIndirectLightLerp((nDotU + 1) * 0.5);
}

#else
half3 GetIndirectLight(half nDotU)
{
  return PSC_AE.rgb;
}
#endif

Light GetLight(half3 ambient, half3 specular, half4 x_x_nDotU_nDotL)
{
  // Get the diffuse coefficient
  half coefDiffuse = max(x_x_nDotU_nDotL.w, 0);
  half coefDiffuseBack = max(-x_x_nDotU_nDotL.w, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + GetIndirectLight(x_x_nDotU_nDotL.z) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse;
  light.specular = specular;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

Light GetLightAS(half3 ambient, half3 specular, half4 x_x_nDotU_nDotL, half4 ambientShadow)
{
  // Get the diffuse coefficient
  half coefDiffuse = max(x_x_nDotU_nDotL.w, 0);
  half coefDiffuseBack = max(-x_x_nDotU_nDotL.w, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + (GetIndirectLight(x_x_nDotU_nDotL.z) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack) * ambientShadow.g;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse;
  light.specular = specular;
  light.specularEnv = 0;
  light.diffuseShadowCoef = ambientShadow.b;
  return light;
}

half4 GetIrradianceData(half3 lightLocal, half3 upLocal, half3 halfway,
                        half4 normal4, sampler2D samplerIrradiance, out half nDotL, out half nDotU)
{
  // Get the values from the irradiance map
  half3 normal = normal4.rgb;
  normal = normal * 2 - 1;
  nDotL = dot(normal, lightLocal);
  nDotU = dot(normal, upLocal);
  half u = nDotL;
  half v = dot(normal, halfway);
  return tex2D(samplerIrradiance, half2(u, v));
}

Light GetLightIrradiance(half3 ambient, half4 irradiance, half specularCoef, half nDotL, half nDotU)
{
  // Fill out the light structure
  half coefDiffuseBack = max(-nDotL, 0);
  Light light;
  light.indirect = ambient + GetIndirectLight(nDotU) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  light.direct = PSC_DForced + PSC_Diffuse.rgb * irradiance.rgb;
  light.specular = PSC_Specular.rgb * irradiance.a * specularCoef;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

Light GetLightNormal(half3 ambient, half3 specular,
                     half4 normal4, half3 lightLocal, half3 upLocal, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse coefficient  
  half nDotL = dot(normal,lightLocal);
  coefDiffuse = max(nDotL, 0);
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse;
  light.specular = specular;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

Light GetLightNormalAS(half3 ambient, half3 specular,
                       half4 normal4, half3 lightLocal, half3 upLocal, half4 ambientShadow, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse coefficient  
  half nDotL = dot(normal,lightLocal);
  coefDiffuse = max(nDotL, 0);
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + (GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack * coefDiffuseBack) * ambientShadow.g;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse;
  light.specular = specular;
  light.specularEnv = 0;
  light.diffuseShadowCoef = ambientShadow.b;
  return light;
}

Light GetLightNormalSpecular(half3 ambient,
                             half4 normal4, half3 lightLocal, half3 halfway, half3 upLocal,
                             half4 specular, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse and specular coefficients
  half nDotL = dot(normal,lightLocal);
  half4 litVector = lit(nDotL, dot(normal, halfway), PSC_Specular.a * specular.b);
  coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse * specular.r;
  light.specular = PSC_Specular * coefSpecular * specular.g;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

Light GetLightNormalSpecularDI(half3 ambient,
                               half4 normal4, half3 lightLocal, half3 halfway, half3 upLocal,
                               half4 specular, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse and specular coefficients
  half nDotL = dot(normal,lightLocal);
  half4 litVector = lit(nDotL, dot(normal, halfway), PSC_Specular.a * specular.b);
  coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse * (1 - specular.g);
  light.specular = PSC_Specular * coefSpecular * specular.g;
  light.specularEnv = 0;
  light.diffuseShadowCoef = 1;
  return light;
}

Light GetLightNormalSpecularAS(half3 ambient,
                               half4 normal4, half3 lightLocal, half3 halfway, half3 upLocal,
                               half4 specular, half4 ambientShadow, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse and specular coefficients
  half nDotL = dot(normal,lightLocal);
  half4 litVector = lit(nDotL, dot(normal, halfway), PSC_Specular.a * specular.b);
  coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + (GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack) * ambientShadow.g;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse * specular.r;
  light.specular = PSC_Specular * coefSpecular * specular.g;
  light.specularEnv = 0;
  light.diffuseShadowCoef = ambientShadow.b;
  return light;
}

Light GetLightNormalSpecularDIAS(half3 ambient,
                                 half4 normal4, half3 lightLocal, half3 halfway, half3 upLocal,
                                 half4 specular, half4 ambientShadow, out half coefDiffuse)
{
  // Get the normal
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  // Get the diffuse and specular coefficients
  half nDotL = dot(normal,lightLocal);
  half4 litVector = lit(nDotL, dot(normal, halfway), PSC_Specular.a * specular.b);
  coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = max(-nDotL, 0);

  // Fill out the light structure
  Light light;
  light.indirect = ambient + (GetIndirectLight(dot(normal,upLocal)) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack) * ambientShadow.g;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse * (1 - specular.g);
  light.specular = PSC_Specular * coefSpecular * specular.g;
  light.specularEnv = 0;
  light.diffuseShadowCoef = ambientShadow.b;
  return light;
}

Light GetLightSuper(half3 ambient, half3 lwsNormal, half3 lwsHalfway, half3 envColor, half fresnelCoef,
                    half4 specular, half4 ambientShadow, out half coefDiffuse)
{
  // Get the diffuse and specular coefficients
  half nDotL = dot(lwsNormal, PSC_WLight);
  half4 litVector = lit(nDotL, dot(lwsNormal, lwsHalfway), PSC_Specular.a * specular.b);
  coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = max(-nDotL, 0);

  // Get the specular coefficient
  half specularCoef = fresnelCoef * specular.g;

  // Fill out the light structure
  Light light;
  light.indirect = ambient + (GetIndirectLight(lwsNormal.y) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack) * ambientShadow.g;
  light.direct = PSC_DForced + PSC_Diffuse * coefDiffuse * (1 - specularCoef);
  light.specular = PSC_Specular * coefSpecular * specularCoef;
  light.specularEnv = envColor * PSC_GlassMatSpecular * 2.0f * specularCoef; // 2 is here because specular color is halved
  light.diffuseShadowCoef = ambientShadow.b;
  return light;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Functions to retrieve the color and alpha from textures

half4 GetColorDiffuse(half4 diffuse, half alphaMul, half alphaAdd)
{
  return half4(diffuse.rgb, diffuse.a * alphaMul + alphaAdd);
}

half4 GetColorDiffuseDXTADiscrete(half4 diffuse, half alphaMul)
{
  half dalpha = (diffuse.a * alphaMul) > 0.5;
  return half4((1 - diffuse.a) * PSC_T0AvgColor.rgb + diffuse.rgb, dalpha);
}

/*
half4 GetColorDiffuseDiscreteNoise(half4 diffuse, half mul, half add, bool aToC)
{
  // adjust the alpha value - can be used to control tree density
  half alpha = diffuse.a*mul;
  half dalpha = pow(alpha*PSC_TreeAdjust.x,PSC_TreeAdjust.y)*PSC_TreeAdjust.z + PSC_TreeAdjust.w;
  if (!aToC)
  {
    //dalpha = 1;
    // Label: AlphaToCoverage
    // on X360 we use alpha to coverage (D3DRS_ALPHATOMASKENABLE)
    // TODO: adding some little noise could help
    // adding 1/N noise, where N is number of samples (4 with no AA)
    //return half4(diffuse.rgb, (diffuse.a*mul+add*PSC_DitherCoef.w) * PSC_SBTSize_invSBTSize_X_AlphaMult.w);
    #if !_VBS3
      // we are adding noise - random value from 0 to 1
      dalpha += add-0.5;
    #endif
    dalpha = dalpha>0.5; // discretize (only 0 or 1 as as a result)
    clip(dalpha-0.5);
  }
  else
  {
//     // to prevent dithering, we want to output nicely rounded value
//     // 0, 1/4, 2/4, 3/4, 4/4
//     if (dalpha>4.0/4) dalpha = 4.0/4;
//     else if (dalpha>3.0/4) dalpha = 3.0/4+0.01;
//     else if (dalpha>2.0/4) dalpha = 2.0/4+0.01;
//     else if (dalpha>1.0/4) dalpha = 1.0/4+0.01;
//     else dalpha = 0;
    
    // we are adding noise - random value from 0 to 1
    //dalpha += (add-0.5)*0.25;
  }
  return half4(diffuse.rgb, dalpha);
}*/

half4 GetColorDiffuseDiscreteNoiseAdv(half4 diffuse, half mul, bool aToC)
{
  // adjust the alpha value - can be used to control tree density
  half alpha = diffuse.a * mul;
  half dalpha;
  if (!aToC)
  {
    dalpha = alpha;
    dalpha = dalpha>0.5;
    clip(dalpha-0.5);
}
  else
  {
    dalpha = pow( alpha * PSC_TreeAdjust.x, PSC_TreeAdjust.y ) * PSC_TreeAdjust.z + PSC_TreeAdjust.w;
  }
  return half4(diffuse.rgb, dalpha);
}

half4 GetColorDiffuseDiscreteNoise(half4 diffuse, half mul, half add, bool aToC)
{
  // adjust the alpha value - can be used to control tree density
  half alpha = diffuse.a * mul;
  half dalpha;// = pow( alpha * PSC_TreeAdjust.x, PSC_TreeAdjust.y ) * PSC_TreeAdjust.z + PSC_TreeAdjust.w;
  if (!aToC)
  {
    dalpha = alpha;
    #if !_VBS3
      // we are adding noise - random value from 0 to 1
      dalpha += add-0.5;
    #endif
    dalpha = dalpha>0.5; // discretize (only 0 or 1 as as a result)
    clip(dalpha-0.5);
  }
  else
  {
    dalpha = pow( alpha * PSC_TreeAdjust.x, PSC_TreeAdjust.y ) * PSC_TreeAdjust.z + PSC_TreeAdjust.w;
  }
  return half4(diffuse.rgb, dalpha);
}

half4 GetColorDiffuseDetail(half4 diffuse, half3 detail, half alphaMul, half alphaAdd)
{
  return half4(diffuse.rgb * detail * 2.0f, diffuse.a * alphaMul + alphaAdd);
}

half4 GetColorDiffuseMacroAS(half4 diffuse, half4 macro, half alphaMul, half alphaAdd)
{
  return half4(lerp(diffuse.rgb, macro.rgb, macro.a), diffuse.a * alphaMul + alphaAdd);
}

half4 GetColorDiffuseDetailMacroAS(half4 diffuse, half3 detail, half4 macro, half alphaMul, half alphaAdd)
{
  return half4(lerp(diffuse.rgb, macro.rgb, macro.a) * detail.rgb * 2, diffuse.a * alphaMul + alphaAdd);
}

half4 GetColorSuper(half4 diffuse, half3 detail, half4 macro, half alphaMul, half specularCoef)
{
  return half4(lerp(diffuse.rgb, macro.rgb, macro.a) * detail.rgb * 2, diffuse.a * alphaMul);
}

/// used to implement pixel shader-side blending (see also PS_BLEND in cpp sources)
#define PS_BLEND 1

half3 ToRTColorSpace(half3 color)
{
  #if 0 // PS_BLEND
  // caution: pow is exp+log, each of them scalar - following code is 6 instructions
  return pow(color,1.0/2.2);
  #else
  return color;
  #endif
}
half4 ToRTColorSpace(half4 color)
{
  return half4(ToRTColorSpace(color.rgb),color.a);
  //return half4(color.rgb,color.a);
}
half3 ApplySrcAlpha(half3 color, half alpha)
{
  #if PS_BLEND
    // used when PS controlled blending is in use
    return color*alpha;
  #else
    return color;
  #endif
}

half4 ApplySrcAlpha(half4 color)
{
  return half4(ApplySrcAlpha(color.rgb,color.a),color.a);
}

//! Structure to hold output of the pixel fragment
struct FOut
{
  Light light;           // Light contribution
  half4 color;           // Diffuse/Ambient color (like texture content), alpha
  bool psBlend;          // blending mode (add/invsrc) controlled in the pixel shader
  bool addBlend;         // use add blending (use invsrc blending otherwise)
  half fog;              // Fog parameter
  half landShadow;       // Landscape shadow coefficient
  float htCoef;           // Thermal capacity related coefficient // _VBS3_TI
  float addTemp;          // Temperature the shader wish to add // _VBS3_TI
  float airCoolingFactor; // Some objects like grass are well cooled and specify this coefficient to be greater than 0 // _VBS3_TI
  float2 tTIMap;          // Mapping coordinates of the TI map // _VBS3_TI
  half nDotL;             // Dot product between normal and main light - used in thermal imaging to determine orientation towards sun // _VBS3_TI
};

//! FOut initializer
FOut InitFOut()
{
  FOut fout;
  fout.light.indirect = 0;
  fout.light.direct = 0;
  fout.light.specular = 0;
  fout.color = 0;
  fout.psBlend = true;
  fout.addBlend = false;
  fout.fog = 0;
  fout.landShadow = 0;
  fout.htCoef = 1.0f;
  fout.addTemp = 0.0f;
  fout.airCoolingFactor = 0.0f;
  fout.tTIMap = half2(0.5f, 0.5f);
  fout.nDotL = 1;
  return fout;
}

//! Fog calculation
half3 ApplyFog(half3 color, half fog, half fogAlpha = 1.0f)
{
  color.rgb = lerp(PSC_FogColor.rgb * fogAlpha , color.rgb, fog);
  return color;
}

//! Function to retrieve the final color of the shader
half4 GetFinalColorWithShadow(FOut fout, half shadowCoef)
{
  half3 colorBlend = (fout.light.indirect + fout.light.direct * shadowCoef) * fout.color.rgb;
  half3 colorAdd = fout.light.specular * shadowCoef + fout.light.specularEnv;

  // If we want to discretize alpha , then PSC_DiscretizeAlpha.a is 1.0f, otherwise it is 0.0f
  // This piece of code basically rounds the value of fout.color.a (sets it to 0.0f or 1.0f) if PSC_DiscretizeAlpha.a is 1.0f
  // and leaves it without change if PSC_DiscretizeAlpha.a is 0.0f.
  // We need to discretize alpha of alpha-tested surfaces, because they are not alpha-blended (because of z-priming - depth only pass)
  // and fog would cause problems.
  fout.color.a = trunc(fout.color.a + 0.5f*PSC_DiscretizeAlpha.a) + (1.0f - PSC_DiscretizeAlpha.a) * frac(fout.color.a);

  DoAlphaTest(fout.color.a);
 
  //half3 colorAdd = 0;
  // See label FINALBLENDING
  #if PS_BLEND
  if (fout.psBlend)
  {
    // blending control implemented in pixel shader
    // D3DRS_SRCBLEND = D3DBLEND_ONE, D3DRS_DESTBLEND = D3DBLEND_INVSRCALPHA
    // ax * fogged(color) + (1-a) * background
    
    if (fout.addBlend)
    {
      // add blending: a * fogged(color) + background
      // D3DRS_SRCBLEND = D3DBLEND_SRCALPHA, D3DRS_DESTBLEND = D3DBLEND_ONE
      
      // add blending: ax=a, a=0
      // output alpha as close to zero as possible, but not zero (unless alpha really is zero) - alpha test would eliminate the pixels
      return half4(ToRTColorSpace(ApplyFog(colorBlend+colorAdd, fout.fog))*fout.color.a,min(fout.color.a,0.01));
      //return half4(half3(0.01,0,0),0.01);
    }
    else
    {
      // default blending: a * fogged(color) + (1-a) * background
      // D3DRS_SRCBLEND = D3DBLEND_SRCALPHA, D3DRS_DESTBLEND = D3DBLEND_INVSRCALPHA
      
      // use default blending for normal color: ax = a
      // for additive components (reflections) use add blending (no influence at target alpha)
      return half4(ToRTColorSpace(ApplyFog(colorBlend*fout.color.a+colorAdd, fout.fog, fout.color.a)),fout.color.a);
    }
  }
  #endif
  // blending controlled using render states, or using alpha to coverage
  return half4(ToRTColorSpace(ApplyFog(colorBlend+colorAdd, fout.fog)),fout.color.a);
  
  
}

//! Function to retrieve the final color of the shader
half4 GetFinalColor(FOut fout)
{
  half shadowCoef = fout.light.diffuseShadowCoef * fout.landShadow;
  return GetFinalColorWithShadow(fout,shadowCoef);
}

//! Function to retrieve the final color of the shader
half4 GetFinalColorSSSM(FOut fout, float4 tShadowBuffer, float2 vPos)
{
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half shadowDisappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
#ifdef _XBOX
  half shadowCoef = shadowDisappear + tex2Dlod(samplerShadowMap, float4((vPos.xy + PSCGPU_TOffX_TOffY_X_X[0].xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - shadowDisappear);
#else
  half shadowCoef = shadowDisappear + tex2Dlod(samplerShadowMap, float4((vPos.xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - shadowDisappear);
#endif

  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - fout.light.diffuseShadowCoef) * shadowDisappear;

  // Involve land shadow to shadow coefficient
  shadowCoef *= fout.landShadow;

  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);

  return GetFinalColorWithShadow(fout,shadowCoef);
}


//! Function to retrieve the final color of the shader
half4 GetFinalColorSR_Default(FOut fout, float4 tShadowBuffer, float2 vPos)
{
  // Precomputed constants related to texture size
  const float texsize = PSC_SBTSize_invSBTSize_X_AlphaMult.x;
  const float invTexsize = PSC_SBTSize_invSBTSize_X_AlphaMult.y;

  // Calculate the fractional part of the texel addressing  
  float2 dxy = frac(tShadowBuffer.xy * texsize);

  // Calculate weights for bilinear interpolation

  //float4 fxy = half4(dxy, half2(1,1)-dxy);

  float3 pnz = float3(+1,-1,0);
  float4 fxy = dxy.xyxy * pnz.xxyy + pnz.zzxx;

  // float4((1 - dxy.x) * (1 - dxy.y), (1 - dxy.x) * dxy.y, dxy.x * (1 - dxy.y), dxy.x * dxy.y)
  // float4(fxy.z       * fxy.w,       fxy.z       * fxy.y, fxy.x * fxy.w,       fxy.x * fxy.y)
  float4 weights = fxy.zzxx*fxy.wywy;

  // ddx/ddy are constant for shadow maps?
  
  // Get 4 shadow depths
  float4 shadowDepth = float4(
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(0,           invTexsize,0,0)).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(invTexsize,  0,0,0)).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(invTexsize,  invTexsize,0,0)).r
    );

  // Get 4 shadow/not shadow values
  float4 sc = step(tShadowBuffer.z, shadowDepth).xxxx; //shadowDepth>=tShadowBuffer.z;

  // Use weights to get the shadow coefficient
  half shadowCoef = dot(sc, weights);
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half shadowDisappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
  shadowCoef = shadowDisappear + shadowCoef * (1 - shadowDisappear);

  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - fout.light.diffuseShadowCoef) * shadowDisappear;

  // Involve land shadow to shadow coefficient
  shadowCoef *= fout.landShadow;

  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);

  //  float shadowDisappear = saturate((tShadowBuffer.w - 50) / 50);
  //  float shadowCoef = (tex2D(samplerShadowMap, tShadowBuffer.xy).r < tShadowBuffer.z) ? shadowDisappear : 1;
  return GetFinalColorWithShadow(fout,shadowCoef);
}

//! Function to retrieve the final color of the shader
half4 GetFinalColorSR_nVidia(FOut fout, float4 tShadowBuffer, float2 vPos)
{
  float4 nVidiaSTRQ = float4(tShadowBuffer.xyz, 1);
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half shadowDisappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
  half shadowCoef = shadowDisappear + tex2Dlod(samplerShadowMap, nVidiaSTRQ).r * (1 - shadowDisappear);

  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - fout.light.diffuseShadowCoef) * shadowDisappear;

  // Involve land shadow to shadow coefficient
  shadowCoef *= fout.landShadow;

  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);

  return GetFinalColorWithShadow(fout,shadowCoef);
}

//! Function to retrieve the empty color of the shader
half4 GetFinalColorEmpty()
{
  return 0;
}

//! Function to get the e-based logarithm
// _VBS3_TI
float logE(float x)
{
  return log(x);
}

//! Return the temperature of the air
float AirTemperature()
{
  float offset = 0.5f / 16.0f;
  return tex2D(samplerTc, float2(0 + offset, 2.0f/16.0f + offset)).r;
}
// Get coefficient teling us how much are to colors similar: 0 - not similar, 1 - equal
// maxDistance - distance that is already considered to be 0 - not similar
half GetColorSimilarityCoef(half3 colorA, half3 colorB, half maxDistance)
{
  half3 colorDiff = (colorA - colorB);
  half3 colorDiff2 = colorDiff * colorDiff;
  return 1.0f - min(sqrt(colorDiff2.r + colorDiff2.g + colorDiff2.b) * 1.0f / maxDistance, 1.0f);
}
//! Function to retrieve the final color of the shader
// _VBS3_TI
#if _SM2
half4 GetFinalThermal(FOut fout)
{
  // Get the temperature coefficients stored in texture
  // R - thermal capacity factor
  // G - alive factor
  // B - movement factor
  // A - metabolism factor
  float4 tempCoef = tex2D(samplerTI, fout.tTIMap) * float4(fout.htCoef, 1, 1, 1);

  // Calculate the coefficient K
  float halfTime = lerp(PSC_HTMin_HTMax_AFMax_MFMax.x, PSC_HTMin_HTMax_AFMax_MFMax.y, tempCoef.r);

  // The Tc table thermal coefficients are in the exponential scale expE. We have to use the logE to get the table linear addressing
  float halfTimeAddress = logE(halfTime);
  halfTimeAddress = min(max(halfTimeAddress, 0.0f), 11.999f);

  // Load values out of the Tc table
  float halfTimeU = halfTimeAddress * (1.0f/16.0f);
  float offset = 0.5f / 16.0f;
  half tableTcBU = tex2D(samplerTc, float2(halfTimeU + offset,          0 + offset)).r;
  half tableTcBL = tex2D(samplerTc, float2(halfTimeU + offset, 1.0f/16.0f + offset)).r;
  half tableTcWU = tex2D(samplerTc, float2(halfTimeU + offset, 2.0f/16.0f + offset)).r;
  half tableTcWL = tex2D(samplerTc, float2(halfTimeU + offset, 3.0f/16.0f + offset)).r;

  // Do the first of bilinear interpolation - grey
  half greyscale = dot(fout.color.rgb, half3(0.299, 0.587, 0.114));
  half tableTcL = lerp(tableTcBL, tableTcWL, greyscale);
  half tableTcU = lerp(tableTcBU, tableTcWU, greyscale);

  // Do the second of bilinear interpolation - dot
  half tableTc;
  {
    float lightIntensity = lerp(0.5f, max(fout.nDotL, 0), PSC_MFact_TBody_X_HSDiffCoef.w);
    tableTc = lerp(tableTcU, tableTcL, lightIntensity);
  }

  // Calculate the final temperature
  float temperature = lerp(tableTc, PSC_MFact_TBody_X_HSDiffCoef.y, saturate(PSC_MFact_TBody_X_HSDiffCoef.x * tempCoef.a))
    + PSC_HTMin_HTMax_AFMax_MFMax.z * tempCoef.g
    + PSC_HTMin_HTMax_AFMax_MFMax.w * tempCoef.b
    + fout.addTemp;

  // Cool the temperature according to airCoolingFactor (f.i. grass is cooled down a lot)
  temperature = lerp(temperature, AirTemperature(), fout.airCoolingFactor);

  // Encode the temperature into RGB
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);

  // Write temperature into output, consider alpha and proper alpha mode
  // See label FINALBLENDING
#if PS_BLEND
  if (fout.psBlend)
  {
    if (fout.addBlend)
    {
      return half4(ApplyFog(rgbEncodedTemperature, fout.fog)*fout.color.a,min(fout.color.a,0.01));
    }
    else
    {
      return half4(ApplyFog(rgbEncodedTemperature*fout.color.a, fout.fog),fout.color.a);
    }
  }
#endif
  return half4(ApplyFog(rgbEncodedTemperature, fout.fog),fout.color.a);
}
#else
half4 GetFinalThermal(FOut fout)
{
  // Get the temperature coefficients stored in texture
  // R - thermal capacity factor
  // G - alive factor
  // B - movement factor
  // A - metabolism factor
  float4 tempCoef = tex2D(samplerTI, fout.tTIMap) * float4(PSC_MFact_TBody_X_HSDiffCoef.z * fout.htCoef, 1, 1, 1);

  // Calculate the coefficient K
  float halfTime = lerp(PSC_HTMin_HTMax_AFMax_MFMax.x, PSC_HTMin_HTMax_AFMax_MFMax.y, tempCoef.r);

  // The Tc table thermal coefficients are in the exponential scale expE. We have to use the logE to get the table linear addressing
  float halfTimeAddress = logE(halfTime);
  halfTimeAddress = min(max(halfTimeAddress, 0.001f), 11.999f);
  float halfTimeFloor = floor(halfTimeAddress);
  float halfTimeFactor = frac(halfTimeAddress);

  // Load values out of the Tc table
  float u0 = halfTimeFloor * (1.0f/16.0f);
  float u1 = u0 + (1.0f/16.0f);
  float offset = 0.5f / 16.0f;
  half tableTcBU = lerp(tex2D(samplerTc, float2(u0 + offset,          0 + offset)).r, tex2D(samplerTc, float2(u1 + offset,          0 + offset)).r, halfTimeFactor);
  half tableTcBL = lerp(tex2D(samplerTc, float2(u0 + offset, 1.0f/16.0f + offset)).r, tex2D(samplerTc, float2(u1 + offset, 1.0f/16.0f + offset)).r, halfTimeFactor);
  half tableTcWU = lerp(tex2D(samplerTc, float2(u0 + offset, 2.0f/16.0f + offset)).r, tex2D(samplerTc, float2(u1 + offset, 2.0f/16.0f + offset)).r, halfTimeFactor);
  half tableTcWL = lerp(tex2D(samplerTc, float2(u0 + offset, 3.0f/16.0f + offset)).r, tex2D(samplerTc, float2(u1 + offset, 3.0f/16.0f + offset)).r, halfTimeFactor);

  // Do the first of bilinear interpolation - grey
  half greyscale = dot(fout.color.rgb, half3(0.299, 0.587, 0.114));
  half tableTcL = lerp(tableTcBL, tableTcWL, greyscale);
  half tableTcU = lerp(tableTcBU, tableTcWU, greyscale);

  // Do the second of bilinear interpolation - dot
  half tableTc;
  {
    float lightIntensity = 0.25f * lerp(0.5f, max(fout.nDotL, 0), PSC_MFact_TBody_X_HSDiffCoef.w);
    tableTc = lerp(tableTcU, tableTcL, lightIntensity);
  }

  // Calculate the final temperature
   float temperature = lerp(tableTc, PSC_MFact_TBody_X_HSDiffCoef.y, saturate(PSC_MFact_TBody_X_HSDiffCoef.x * tempCoef.a))
     + PSC_HTMin_HTMax_AFMax_MFMax.z * tempCoef.g
    + PSC_HTMin_HTMax_AFMax_MFMax.w * tempCoef.b
    + fout.addTemp;

  // Cool the temperature according to airCoolingFactor (f.i. grass is cooled down a lot)
  temperature = lerp(temperature, AirTemperature(), fout.airCoolingFactor);

  // Encode the temperature into RGB
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);

  // Write temperature into output, consider alpha and proper alpha mode
  // See label FINALBLENDING
#if PS_BLEND
  if (fout.psBlend)
  {
    if (fout.addBlend)
    {
      return half4(ApplyFog(rgbEncodedTemperature, fout.fog)*fout.color.a,min(fout.color.a,0.01));
    }
    else
    {
      return half4(ApplyFog(rgbEncodedTemperature*fout.color.a, fout.fog),fout.color.a);
    }
  }
#endif
  return half4(ApplyFog(rgbEncodedTemperature, fout.fog),fout.color.a);
}
#endif

float3 EncodeTemperature(float temperature) 
{
  float3 result;
  EncodeTemperatureToRGB(temperature, result.r, result.g, result.b);
  
  return result;
}

static float TempPlus = 1.9;
static float TempMinus = 0.1;

half4 GetFinalThermalSimpleNoMultAlpha(FOut fout)
{
  half3 clr = fout.color.rgb;

  if (fout.psBlend)
  {
    if (fout.addBlend)
    {
      clr = ApplyFog(fout.color.rgb, fout.fog) * fout.color.a;
    }
    else
    {
      clr = ApplyFog(fout.color.rgb * fout.color.a, fout.fog);
    }
  }

  half greyscale = dot(clr.rgb, half3(0.299, 0.587, 0.114)); 

  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  half airTemperature = AirTemperature();

  // Change object temperature - based on greyscale color
  float temperature = lerp(airTemperature - TempMinus, airTemperature + lerp(TempPlus, 0.0, fout.airCoolingFactor), greyscale);
  // Add some special temperature
  temperature += fout.addTemp;

  float3 encodedTemp = EncodeTemperature(temperature);
 
  return half4(encodedTemp.rgb, 1.0);
}

// Color multiplied with alpha
half4 GetFinalThermalSimple(FOut fout)
{
  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  half airTemperature = AirTemperature();
  half greyscale = dot(fout.color.rgb, half3(0.299, 0.587, 0.114)); 

  // Change object temperature - based on greyscale color
  float temperature = lerp(airTemperature - TempMinus, airTemperature + lerp(TempPlus, 0.0, fout.airCoolingFactor), greyscale);

  // Add some special temperature
  temperature += fout.addTemp;
  float3 encodedTemp = EncodeTemperature(temperature);

#if PS_BLEND
  if (fout.psBlend)
  {
    if (fout.addBlend)
    {
      return half4(ApplyFog(encodedTemp, fout.fog) * fout.color.a, min(fout.color.a, 0.01));
    }
    else
    {
      return half4(ApplyFog(encodedTemp * fout.color.a, fout.fog), fout.color.a);
    }
  }
#endif

  return half4(ApplyFog(encodedTemp, fout.fog), fout.color.a);
}

half4 GetFinalThermalSimple1(half4 clr, half4 addTemp_airCooling_fog, bool psBlend, bool addBlend)
{
  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  half airTemperature = AirTemperature();
  half greyscale = dot(clr.rgb, half3(0.299, 0.587, 0.114));

  // Change object temperature - based on greyscale color
  float temperature = lerp(airTemperature - TempMinus, airTemperature + TempPlus, greyscale);
  temperature += addTemp_airCooling_fog.x;

  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);

#if PS_BLEND
  if (psBlend)
  {
    if (addBlend)
    {
      return half4(ApplyFog(rgbEncodedTemperature, addTemp_airCooling_fog.z) * clr.a, min(clr.a, 0.01));
    }
    else
    {
      return half4(ApplyFog(rgbEncodedTemperature * clr.a, addTemp_airCooling_fog.z), clr.a);
    }
  }
#endif

  return half4(ApplyFog(rgbEncodedTemperature, addTemp_airCooling_fog.z), clr.a);
}

static const int FinalColor=0;
static const int FinalColorSSSM=2;
static const int FinalColorSR_Default=3;
static const int FinalColorSR_nVidia=4;
static const int FinalThermal=5;

void CallFinalColor(FOut fout, float4 tShadowBuffer, float2 vPos, int type, out half4 oColor)
{
  if (type==FinalColor) {oColor = GetFinalColor(fout);return;}
  if (type==FinalColorSSSM) {oColor = GetFinalColorSSSM(fout,tShadowBuffer,vPos);return;}
  if (type==FinalColorSR_Default) {oColor = GetFinalColorSR_Default(fout,tShadowBuffer,vPos);return;}
  if (type==FinalColorSR_nVidia) {oColor = GetFinalColorSR_nVidia(fout,tShadowBuffer,vPos);return;}
  if (type==FinalThermal) {oColor = GetFinalThermalSimple(fout);return;}
  oColor = GetFinalColorEmpty();
}


//! Function to retrieve the final color of the shader, special for water to blend it in fog
// miva
half4 GetWaterFinalColor(FOut fout)
{
  half shadowCoef = fout.light.diffuseShadowCoef * fout.landShadow;
  
  half3 colorBlend  = (fout.light.indirect + fout.light.direct * shadowCoef) * fout.color.rgb;
  half3 colorAdd    = fout.light.specular * shadowCoef + fout.light.specularEnv;

  return half4(ToRTColorSpace(ApplyFog(colorBlend*fout.color.a + colorAdd, fout.fog, fout.color.a))*fout.fog, fout.color.a);
}


//! miva Macro to create the specified PS - no shadow variants and color multiplied with fog coef
#define CREATEPSSN_NS(name,structureName) \
  OPT_INPUTS void PS##name(structureName input, out half4 oColor: COLOR) \
  { \
    FOut fout = PF##name(input,false,input.vPos); \
    oColor = GetWaterFinalColor(fout); \
  }



//! Macro to create the specified PS - no shadow variants
/*#define CREATEPSSN_NS(name,structureName) \
  OPT_INPUTS void PS##name(structureName input, out half4 oColor: COLOR) \
  { \
    FOut fout = PF##name(input,false); \
    oColor = GetFinalColor(fout); \
  }
*/


//! List of shaders - every pixel shader we want to create various versions for (shadow, no shadow, ...), must be registered here
#if _VBS3
#define PS_LIST(XXC,XXS,XXN,XXD,XXDS, XX6) \
  XXC(Detail)                                                                     \
  XXC(DetailSpecularAlpha)                                                        \
  XXC(Normal)                                                                     \
  XXD(Grass)                                                                      \
  XXS(NormalDXTA,                             SNormal)                            \
  XXC(SpecularAlpha)                                                              \
  XXC(SpecularNormalMapGrass)                                                     \
  XXD(SpecularNormalMapThrough)                                                   \
  XXDS(SpecularNormalMapThroughSimple,         SSpecularNormalMapThrough)         \
  XXD(SpecularNormalMapSpecularThrough)                                           \
  XXDS(SpecularNormalMapSpecularThroughSimple, SSpecularNormalMapSpecularThrough) \
  XXC(DetailMacroAS)                                                              \
  XXC(DetailSpecularAlphaMacroAS)                                                 \
  XXC(NormalMap)                                                                  \
  XXC(NormalMapDetailMacroASSpecularMap)                                          \
  XXC(NormalMapDetailMacroASSpecularDIMap)                                        \
  XXC(NormalMapDetailSpecularMap)                                                 \
  XXC(NormalMapDetailSpecularDIMap)                                               \
  XXC(NormalMapMacroAS)                                                           \
  XXC(NormalMapMacroASSpecularMap)                                                \
  XXC(NormalMapMacroASSpecularDIMap)                                              \
  XXC(NormalMapSpecularMap)                                                       \
  XXC(NormalMapSpecularDIMap)                                                     \
  XXC(SpecularNormalMapDiffuse)                                                   \
  XXC(SpecularNormalMapDiffuseMacroAS)                                            \
  XXS(Terrain15,                              STerrain)                           \
  XXS(TerrainSimple15,                        STerrain)                           \
  XXS(TerrainGrass15,                         STerrain)                           \
  XXS(Road,                                   SRoad)                              \
  XXS(Road2Pass,                              SRoad)                              \
  XXN(ShoreFoam,                              SShore)                             \
  XXN(ShoreWet,                               SShore)                             \
  XXC(Glass)                                                                      \
  XXS(Crater1,                                SCrater)                            \
  XXS(Crater2,                                SCrater)                            \
  XXS(Crater3,                                SCrater)                            \
  XXS(Crater4,                                SCrater)                            \
  XXS(Crater5,                                SCrater)                            \
  XXS(Crater6,                                SCrater)                            \
  XXS(Crater7,                                SCrater)                            \
  XXS(Crater8,                                SCrater)                            \
  XXS(Crater9,                                SCrater)                            \
  XXS(Crater10,                               SCrater)                            \
  XXS(Crater11,                               SCrater)                            \
  XXS(Crater12,                               SCrater)                            \
  XXS(Crater13,                               SCrater)                            \
  XXS(Crater14,                               SCrater)                            \
  XXC(Sprite)                                                                     \
  XXS(SpriteSimple,                           SSprite)                            \
  XXC(Super)                                                                      \
  XXC(Multi)                                                                      \
  XXS(TerrainX,                               STerrain)                           \
  XXS(TerrainSimpleX,                         STerrain)                           \
  XXS(TerrainGrassX,                          STerrain)                           \
  XXD(Tree)                                                                       \
  XXD(TreePRT)                                                                    \
  XXDS(TreeSimple,                             STree)                             \
  XX6(Skin)                                                                       \
  XXDS(TreeAToC, STree)                                                           \
  XXDS(GrassAToC, SGrass)                                                         \
  XXS(TerrainSNX,                             STerrain)                           \
  XXS(VolCloud,                               SSprite)                            \
  XXS(VolCloudSimple,                           SSprite)                          \

#else

#define PS_LIST(XXC,XXS,XXN,XXD,XXDS, XX6) \
  XXC(Detail)                                                                     \
  XXC(DetailSpecularAlpha)                                                        \
  XXC(Normal)                                                                     \
  XXD(Grass)                                                                      \
  XXD(Water)                                                                      \
  XXS(NormalDXTA,                             SNormal)                            \
  XXC(SpecularAlpha)                                                              \
  XXC(SpecularNormalMapGrass)                                                     \
  XXD(SpecularNormalMapThrough)                                                   \
  XXDS(SpecularNormalMapThroughSimple,         SSpecularNormalMapThrough)         \
  XXD(SpecularNormalMapSpecularThrough)                                           \
  XXDS(SpecularNormalMapSpecularThroughSimple, SSpecularNormalMapSpecularThrough) \
  XXC(DetailMacroAS)                                                              \
  XXC(DetailSpecularAlphaMacroAS)                                                 \
  XXC(NormalMap)                                                                  \
  XXC(NormalMapDetailMacroASSpecularMap)                                          \
  XXC(NormalMapDetailMacroASSpecularDIMap)                                        \
  XXC(NormalMapDetailSpecularMap)                                                 \
  XXC(NormalMapDetailSpecularDIMap)                                               \
  XXC(NormalMapMacroAS)                                                           \
  XXC(NormalMapMacroASSpecularMap)                                                \
  XXC(NormalMapMacroASSpecularDIMap)                                              \
  XXC(NormalMapSpecularMap)                                                       \
  XXC(NormalMapSpecularDIMap)                                                     \
  XXC(SpecularNormalMapDiffuse)                                                   \
  XXC(SpecularNormalMapDiffuseMacroAS)                                            \
  XXS(Terrain15,                              STerrain)                           \
  XXS(TerrainSimple15,                        STerrain)                           \
  XXS(TerrainGrass15,                         STerrain)                           \
  XXS(Road,                                   SRoad)                              \
  XXS(Road2Pass,                              SRoad)                              \
  XXN(Shore,                                  SShore)                             \
  XXN(ShoreFoam,                              SShore)                             \
  XXN(ShoreWet,                               SShore)                             \
  XXC(Glass)                                                                      \
  XXS(Crater1,                                SCrater)                            \
  XXS(Crater2,                                SCrater)                            \
  XXS(Crater3,                                SCrater)                            \
  XXS(Crater4,                                SCrater)                            \
  XXS(Crater5,                                SCrater)                            \
  XXS(Crater6,                                SCrater)                            \
  XXS(Crater7,                                SCrater)                            \
  XXS(Crater8,                                SCrater)                            \
  XXS(Crater9,                                SCrater)                            \
  XXS(Crater10,                               SCrater)                            \
  XXS(Crater11,                               SCrater)                            \
  XXS(Crater12,                               SCrater)                            \
  XXS(Crater13,                               SCrater)                            \
  XXS(Crater14,                               SCrater)                            \
  XXC(Sprite)                                                                     \
  XXS(SpriteSimple,                           SSprite)                            \
  XXC(Super)                                                                      \
  XXC(Multi)                                                                      \
  XXS(TerrainX,                               STerrain)                           \
  XXS(TerrainSimpleX,                         STerrain)                           \
  XXS(TerrainGrassX,                          STerrain)                           \
  XXD(Tree)                                                                       \
  XXD(TreePRT)                                                                    \
  XXDS(TreeSimple,                             STree)                             \
  XX6(Skin)                                                                       \
  XXDS(TreeAToC, STree)                                                           \
  XXDS(GrassAToC, SGrass)                                                         \
  XXDS(TreeSN,                                 STree)                             \
  XXS(SpriteExtTi,                            SSprite)                            \
  XXS(TerrainSNX,                             STerrain)                           \
  XXS(VolCloud,                               SSprite)                            \
  XXS(VolCloudSimple,                           SSprite)                          \
  XXC(SuperExt)                                                                   \
  XXS(SuperHair, SSuper)                                                          \
  XXS(SuperHairAtoC, SSuper)                                                          \
  XXS(Refract, SRefract)                           /* _ARMA3_REFRACTION */        \
  XXS(SpriteRefract, SSprite)                    /*_ARMA3_REFRACTION_SPRITES*/    \
  XXS(SpriteRefractSimple, SSprite)  /*_ARMA3_REFRACTION_SPRITES - no depth*/     \
  XXC(SuperAToC) \

#endif

/////////////////////////////////////////////////////////////////////////////////////////////
// P&S - Point and Spot lights

#define LPOINT_TransformedPos(index) PSC_LPSData[index*4 + 0]
#define LPOINT_Atten(index)          PSC_LPSData[index*4 + 1]
#define LPOINT_D(index)              PSC_LPSData[index*4 + 2]
#define LPOINT_A(index)              PSC_LPSData[index*4 + 3]

#define LSPOT_TransformedPos_CosPhiHalf(index)   PSC_LPSData[index*4 + 0]
#define LSPOT_TransformedDir_CosThetaHalf(index) PSC_LPSData[index*4 + 1]
#define LSPOT_D_InvCTHMCPH(index)                PSC_LPSData[index*4 + 2]
#define LSPOT_A_Atten(index)                     PSC_LPSData[index*4 + 3]

float3 PointSpotLighting(in float3 skinnedPosition, in float3 skinnedNormal)
{
  // Color accumulator
  float3 sumColor = float3(0, 0, 0);

  // Saturation value
#if _VBS3_PERPIXELPSLIGHTS
  const float saturationCoef = 3;
#else
  const float saturationCoef = 0.3;
#endif

  // Point lights
  for (int i = 0; i < PSC_PointLoopCount.x; i++)
  {
    float3 pointToLightVector = LPOINT_TransformedPos(i).xyz - skinnedPosition.xyz;
    float lightSquareDistance = dot(pointToLightVector, pointToLightVector);
    float invLightDistance = rsqrt(lightSquareDistance);
    float3 lightDirection = pointToLightVector * invLightDistance;
    float attenuation = LPOINT_Atten(i).x * invLightDistance * invLightDistance;
    sumColor += (max(0, dot(skinnedNormal, lightDirection)) * LPOINT_D(i).xyz + LPOINT_A(i).xyz) * min(saturationCoef, attenuation);
  }

  // Spot lights
  for (int ii = 0; ii < PSC_SpotLoopCount.x; ii++)
  {
    int i = MAX_LIGHTS - ii - 1;
    float3 pointToLightVector = LSPOT_TransformedPos_CosPhiHalf(i).xyz - skinnedPosition.xyz;
    float lightSquareDistance = dot(pointToLightVector, pointToLightVector);
    float invLightDistance = rsqrt(lightSquareDistance);
    float3 lightDirection = pointToLightVector * invLightDistance;
    float rho = dot(-LSPOT_TransformedDir_CosThetaHalf(i).xyz, lightDirection);
    float spotlightFactor = (rho > LSPOT_TransformedDir_CosThetaHalf(i).w) ? 1 : (rho <= LSPOT_TransformedPos_CosPhiHalf(i).w) ? 0 : (rho - LSPOT_TransformedPos_CosPhiHalf(i).w) * LSPOT_D_InvCTHMCPH(i).w;
    float attenuation = LSPOT_A_Atten(i).w * invLightDistance * invLightDistance;
    sumColor += (max(0, dot(skinnedNormal, lightDirection)) * LSPOT_D_InvCTHMCPH(i).xyz + LSPOT_A_Atten(i).xyz) * min(saturationCoef, attenuation * spotlightFactor);
  }

  // Return the ambient light
  return sumColor;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Pixel shaders

FOut PFDetail(SDetail input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    input.x_x_nDotU_nDotL);

  // Calculate the color
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tDiffuseMap_DetailMap.xy),
    tex2D(sampler1, input.tDiffuseMap_DetailMap.wzyx),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFDetailSpecularAlpha(SDetailSpecularAlpha input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    input.specularColor_landShadow.rgb,
    input.x_x_nDotU_nDotL);

  // Calculate the color
  half3 s = input.specularColor_landShadow.rgb;
  half specIntensity = dot(s, s) * 2.0f;
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tDiffuseMap_DetailMap.xy),
    tex2D(sampler1, input.tDiffuseMap_DetailMap.wzyx),
    input.ambientColor.a, specIntensity);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormal(SNormal input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    input.x_x_nDotU_nDotL);

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

//#define EARLY_OUT

#ifdef EARLY_OUT
const static bool EarlyOut = true;
const static bool RejectAll = false;
  #define FORCEBRANCH [branch]
#else
const static bool EarlyOut = false;
const static bool RejectAll = false;
  #define FORCEBRANCH
#endif

void PFGrassBody(SGrass input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor, bool aToC)
{
  FOut fout = InitFOut();

  // Calculate the lights
  // Note: for ground clutter we are not using lighting based on normal at all
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    input.x_x_nDotU_nDotL);

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap.xy),
    input.ambientColor.a, 0);
    
  fout.color.rgb *= input.lightingColor.rgb;

  FORCEBRANCH if (!EarlyOut || input.ambientColor.a > 0 && !RejectAll)
  {
    if (!aToC)
    {
      // perform alpha discretization
      fout.color.a = fout.color.a>0.5;
    }

    fout.psBlend = false; // never using blending, avoid modulating color by alpha

    // Calculate the shadow intensity

    //if (sbShadows) Optimization in else branch (indirect light set to 0 causes the shader to have less instructions) is not correct
    {
      // Calculate the land shadow
      fout.landShadow = input.landShadow.a;
    }
//     else
//     {
//       // we can provide result value directly - separate direct/indirect not needed
//       // set up color coefficients so that Final functions use the color we have already calculated
//       fout.light.indirect = 0;
//       fout.light.direct *= input.lightingColor.w;
//       fout.landShadow = 1;
//     }
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = input.x_x_nDotU_nDotL.w;
    fout.airCoolingFactor = 0.5;
    fout.htCoef = 0.5;

    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}

void PFGrass(SGrass input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFGrassBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,false);
}

void PFGrassAToC(SGrass input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFGrassBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,true);
}

//////////////////////////////////////////////

FOut PFNormalDXTA(SNormal input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    input.x_x_nDotU_nDotL);

  // Calculate the color
  fout.color = GetColorDiffuseDXTADiscrete(tex2D0(input.tDiffuseMap.xy), input.ambientColor.a);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFSpecularAlpha(SSpecularAlpha input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    input.specularColor_landShadow.rgb,
    input.x_x_nDotU_nDotL);

  // Calculate the color
  half3 s = input.specularColor_landShadow.rgb;
  half specIntensity = dot(s, s) * 2.0f;
  
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap.xy),
    input.ambientColor.a, specIntensity); /** half4(0, 1, 0, 1)*/

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Ti texture coords
  fout.tTIMap = input.tDiffuseMap;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFSpecularNormalMapGrass(SSpecularNormalMapGrass input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  {
    // Indirect light
    fout.light.indirect = input.ambientColor.rgb;

    // Calculate the direct light
    half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
    half frontCoef = max(dot(normalMap.rgb * 2 - 1, input.lightLocal_landShadow.rgb), 0);
    half backCoef = max(dot(-(normalMap.rgb * 2 - 1), input.lightLocal_landShadow.rgb), 0);
    fout.light.direct = frontCoef * PSC_Diffuse.rgb + backCoef * PSC_LDirectionGround_DiffuseBack.rgb;

    // No specular light
    fout.light.specular = 0;
    fout.light.specularEnv = 0;
    
    fout.light.diffuseShadowCoef = 1;
  }

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.lightLocal_landShadow.a;

  // Remember the nDotL
  fout.nDotL = 1; // Grass is a special object - it actually doesn't matter where the sun is shining from

  // We know we don't want blending for grass
  fout.psBlend = false;

  return fout;
}

//////////////////////////////////////////////

void PFSpecularNormalMapThrough(
  SSpecularNormalMapThrough input, bool sbShadows, float4 tShadowBuffer, float2 vPos,
  int finalColorType, out half4 oColor
)
{
  FOut fout = InitFOut();
  
  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 ambientAtten = tex2D(sampler3, input.tDiffuseAtten_AmbientAtten.wzyx);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half diffuseAtten = tex2D(sampler2, input.tDiffuseAtten_AmbientAtten.xy).a;

  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half da;
    {
      // perform ambient attenuation
      fout.light.indirect = ambientAtten.rgb * input.ambientColor.rgb + GetIndirectLight(dot(normalMap.rgb * 2 - 1, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontCoef = max(dot(normalMap.rgb * 2 - 1, input.lightLocal_landShadow.rgb), 0);
      half backCoef = max(dot(-(normalMap.rgb * 2 - 1), input.lightLocal_landShadow.rgb), 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = diffuseAtten * lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      da = diffuseAtten * frontCoef;

      // No specular light
      fout.light.specular = 0;
      fout.light.specularEnv = 0;
      
      fout.light.diffuseShadowCoef = 1;
    }

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise(primTex, input.ambientColor.a, normalMap.a, false);

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = da;

    // We know we don't want blending for trees
    fout.psBlend = false;

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}

//////////////////////////////////////////////

void PFSpecularNormalMapThroughSimple(
  SSpecularNormalMapThrough input, bool sbShadows, float4 tShadowBuffer, float2 vPos,
  int finalColorType, out half4 oColor
)
{
  FOut fout = InitFOut();

  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half da;
    {
      // Ambient light
      fout.light.indirect = input.ambientColor.rgb + GetIndirectLight(dot(normalMap.rgb * 2 - 1, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontCoef = max(dot(normalMap.rgb * 2 - 1, input.lightLocal_landShadow.rgb), 0);
      half backCoef = max(dot(-(normalMap.rgb * 2 - 1), input.lightLocal_landShadow.rgb), 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      da = frontCoef;

      // No specular light
      fout.light.specular = 0;
      fout.light.specularEnv = 0;
      
      fout.light.diffuseShadowCoef = 1;
    }

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise(primTex, input.ambientColor.a, normalMap.a , false);

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = da;

    // We know we don't want blending for trees
    fout.psBlend = false;

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}

void PFSpecularNormalMapSpecularThrough(
  SSpecularNormalMapSpecularThrough input, bool sbShadows, float4 tShadowBuffer, float2 vPos,
  int finalColorType, out half4 oColor
)
{
  FOut fout = InitFOut();

  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 ambientAtten = tex2D(sampler3, input.tDiffuseAtten_AmbientAtten.wzyx);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half diffuseAtten = tex2D(sampler2, input.tDiffuseAtten_AmbientAtten.xy).a;
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half da;
    {
      half3 normal = normalMap.rgb * 2 - 1;

      // perform ambient attenuation
      
      fout.light.indirect = ambientAtten.rgb * input.ambientColor.rgb + GetIndirectLight(dot(normal, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontBack = dot(normal, input.lightLocal_landShadow.rgb);
      half frontCoef = max(frontBack, 0);
      half backCoef = max(-frontBack, 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = diffuseAtten * lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      da = diffuseAtten * lightCoef;

      // Calculate specular light
      half4 litVector = lit(frontBack, dot(normal, input.tHalfway.xyz), PSC_Specular.a);
      half coefSpecular = litVector.z;
      coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
      fout.light.specular = PSC_Specular * coefSpecular * diffuseAtten;
      fout.light.specularEnv = 0;

      fout.light.diffuseShadowCoef = 1;
    }

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise(primTex,input.ambientColor.a, normalMap.a, false);

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = da;

    // We know we don't want blending for trees
    fout.psBlend = false;

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}

void PFSpecularNormalMapSpecularThroughSimple(
  SSpecularNormalMapSpecularThrough input, bool sbShadows, float4 tShadowBuffer, float2 vPos,
  int finalColorType, out half4 oColor
)
{
  FOut fout = InitFOut();

  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half da;
    {
      half3 normal = normalMap.rgb * 2 - 1;

      // Ambient light
      fout.light.indirect = input.ambientColor.rgb + GetIndirectLight(dot(normal, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontBack = dot(normal, input.lightLocal_landShadow.rgb);
      half frontCoef = max(frontBack, 0);
      half backCoef = max(-frontBack, 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      da = lightCoef;

      // Calculate specular light
      half4 litVector = lit(frontBack, dot(normal, input.tHalfway.xyz), PSC_Specular.a);
      half coefSpecular = litVector.z;
      coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
      fout.light.specular = PSC_Specular * coefSpecular;
      fout.light.specularEnv = 0;

      fout.light.diffuseShadowCoef = 1;
    }

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise(primTex, input.ambientColor.a, normalMap.a, false);

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = da;

    // We know we don't want blending for trees
    fout.psBlend = false;

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}


void PFTreeBody(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor, bool simple, bool aToC, bool sNoise )
{
  FOut fout = InitFOut();

  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half4 macroMap = tex2D(sampler2, input.tShadowMap.xy);
  half ambientAtten = macroMap.a;
  half diffuseAtten = lerp(1, macroMap.a, PSC_Diffuse.a);
  
  if (simple) ambientAtten = 1, diffuseAtten = 1;
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half da;
    {
      half3 normal = normalMap.rgb * 2 - 1;

      // perform ambient attenuation

      fout.light.indirect = ambientAtten * input.ambientColor.rgb + GetIndirectLight(dot(normal, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontBack = dot(normal, input.lightLocal_landShadow.rgb);
      half frontCoef = max(frontBack, 0);
      half backCoef = max(-frontBack, 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = diffuseAtten * lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      da = diffuseAtten * lightCoef;

      // Calculate specular light
      half4 litVector = lit(frontBack, dot(normal, input.tHalfway.xyz), PSC_Specular.a);
      half coefSpecular = litVector.z;
      coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
      fout.light.specular = PSC_Specular * coefSpecular * diffuseAtten;
      fout.light.specularEnv = 0;
      
      if (simple) fout.light.specular = 0;

      fout.light.diffuseShadowCoef = 1;
    }

 // simple noise (_CAN and _NO textures instead of _CA and _NON)
 #ifdef NEW_TREE_NOISE
    if( sNoise )
      fout.color = GetColorDiffuseDiscreteNoiseAdv( primTex * half4(macroMap.rgb, 1), input.ambientColor.a, aToC );
    else
  #endif
    fout.color = GetColorDiffuseDiscreteNoise( primTex * half4(macroMap.rgb, 1), input.ambientColor.a, normalMap.a, aToC );

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = da;

    fout.psBlend = false; // never using blending, avoid modulating color by alpha

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
}

void PFTree(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFTreeBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,false, false, false );
}

void PFTreeSN(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFTreeBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,false, false, true );
}

void PFTreeAToC(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFTreeBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,false, true, false );
}

void PFTreeSimple(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  PFTreeBody(input,sbShadows,tShadowBuffer,vPos,finalColorType,oColor,true, false, false );
}

void PFTreePRT(STreePRT input, bool sbShadows, float4 tShadowBuffer, float2 vPos, int finalColorType, out half4 oColor)
{
  FOut fout = InitFOut();

#if _SM2
  oColor = (1, 1, 0, 1); // Purple color will show the shaders are not implemented
#else
  half4 primTex = tex2D0(input.tDiffuseMap_prtMap.xy);
  half4 prtMap = tex2D(sampler1, input.tDiffuseMap_prtMap.wzyx + input.tPRTOffsfet.xy);
  half4 macroMap = tex2D(sampler2, input.tMacroMap.xy + input.tPRTOffsfet.xy);
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate direct light
    float4 vAccumDirectR = prtMap * PSC_PRTConstantDirect[0];
    float4 vAccumDirectG = prtMap * PSC_PRTConstantDirect[1];
    float4 vAccumDirectB = prtMap * PSC_PRTConstantDirect[2];
    float3 vLightDirect;
    vLightDirect.r = dot(vAccumDirectR,1);
    vLightDirect.g = dot(vAccumDirectG,1);
    vLightDirect.b = dot(vAccumDirectB,1);

    // Calculate indirect light
    float4 vAccumIndirectR = prtMap * PSC_PRTConstantIndirect[0];
    float4 vAccumIndirectG = prtMap * PSC_PRTConstantIndirect[1];
    float4 vAccumIndirectB = prtMap * PSC_PRTConstantIndirect[2];
    float3 vLightIndirect;
    vLightIndirect.r = dot(vAccumIndirectR,1);
    vLightIndirect.g = dot(vAccumIndirectG,1);
    vLightIndirect.b = dot(vAccumIndirectB,1);

    // Calculate the lights and diffuse attenuation
    fout.light.indirect = vLightIndirect + input.ambientColor;
    fout.light.direct = vLightDirect;
    fout.light.specular = 0;
    fout.light.specularEnv = 0;
    fout.light.diffuseShadowCoef = 1;

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise(primTex * half4(macroMap.rgb, 1), input.ambientColor.a, 0, false);

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = 1; // TreePRT is a special object - it actually doesn't matter where the sun is shining from

    // We know we don't want blending for trees
    fout.psBlend = false;

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
    CallFinalColor(fout,tShadowBuffer,vPos,finalColorType,oColor);
  }
  else
  {
    oColor = GetFinalColorEmpty();
  }
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// ADVANCED TREE
////////////////////////////////////////////////////////////////////////////////////////////////////

//==================================================================================================
void PFTreeAdv1( inout SPFTreeAdv s, STreeAdv i, bool crown, bool simple, bool aToC, bool thermal )
{
  //simple = true;

  half4 final;
  float2 wrap1 = i.tc0.xy;
  float2 wrapNormal = i.tc0.wz;
  //float2 wrap2 = i.tShadowMap.xy;
  float2 wrap2 = i.tc1.xy;
  half4 tex0 = tex2D0(wrap1);
  half4 tex1 = tex2D(sampler1, wrapNormal);
  half4 tex2 = tex2D(sampler2, wrap2);
  //half4 tex3 = tex2D(sampler3, wrap2);
  
  // local direction to camera
  half3 dirToCamL = i.tc2.xyz;
  half3 sumVertLight = i.ambientColor.rgb;
// FROM CONST REGS:
  half ppBleedMUL = PSC_TreeAdvPars[0].z; //0.9;
  half ppBleedADD = PSC_TreeAdvPars[0].w; //0.15;
  half ppTranslucencyMul = PSC_TreeAdvPars[1].x; // -0.8;
  half ppTranslucencyAdd = PSC_TreeAdvPars[1].y; // -0.3;
  half3 crownTranslucencyColor1 = PSC_TreeAdvPars[2].rgb; //half3( 0.8, 0.7, 0.5 );
  half3 crownTranslucencyColor2 = PSC_TreeAdvPars[3].rgb; //half3( 0.8, 0.7, 0.5 );
  half3 specularAmbiColor = PSC_TreeAdvPars[4].rgb; //half3( 0.5, 0.5, 0.5 );
  // ambient occlusion
  half ambiOcc = tex2.a;
  half crownCenterAttMul = PSC_TreeAdvPars[1].z; // 3.0;
  half crownCenterAttAdd = PSC_TreeAdvPars[1].w; // -0.5;
  half crownTermA = i.tc1.z;
  half crownCenterTermA = saturate( ambiOcc * crownCenterAttMul + crownCenterAttAdd );
  
  if( crown )
  {
 #ifdef NEW_TREE_NOISE
    final.a = GetColorDiffuseDiscreteNoiseAdv( tex0.rgba, i.ambientColor.a, aToC ).a; 
  #else   
    final.a = GetColorDiffuseDiscreteNoise( tex0.rgba, i.ambientColor.a, tex1.a, aToC ).a;
  #endif  
  }
  else
    final.a = 1;
  // pp local normal
  // pp local normal
  half3 dxt5FixedNormal = half3(1 - tex1.a, 0, 0) + tex1.rgb;
  half3 normal = normalize(dxt5FixedNormal * 2 - 1);

  half3 halfVec = i.tc3.xyz;
  half3 upVec = i.tUpLocal.xyz;
  half3 lightDir = i.lightLocal_landShadow.xyz;
  
  half3 halfVecA = normalize( dirToCamL + upVec );
  
  half specAttMain = 1.0;
  
// DOTs
  half dotNL = dot( normal, lightDir );
  half dotCL = dot( dirToCamL, lightDir );
  half dotCN = dot( dirToCamL, normal );
  half dotHN = dot( halfVec, normal );
  half dotNU = dot( normal, upVec );
  
  half signDotCN = sign( dotCN );
  // pp fresnel value
  half fresnel;
  half fastFresnel0 = 0.8;
  half fr1;
  if( crown )
    fr1 = saturate( 1.0 - fastFresnel0 * dotCN * signDotCN );
  else
    fr1 = saturate( 1.0 - fastFresnel0 * dotCN );
  fresnel = fr1 * fr1;
  fresnel = fresnel * fresnel;
  
  half skyFactor = 0.5 * dotNU + 0.5;
  half skyFactorI = 1.0 - skyFactor;
  half diffuseAtten = crownTermA * crownCenterTermA;
  half shadowCoef = s.shadow;
  // final material color
  half3 colorFinal = tex0.rgb * tex2.rgb * 4.5947;
  
// LIGHTING
  // Light from the environment - roughly Ambient, DForced and Emmisive
  half3 indirectLight;
  // Light from the main light source (light that disappears in a shadow) - roughly the diffuse light
  half3 directLight;
  // dir lights specular
  half3 specularDirLight;
  // ambient specular - specular light from the environment
  half3 specularAmbi;
  // Diffuse shadow value, used where no ordinary shadows are available (f.i. for objects in distance)
  half diffuseShadowCoefLight;
  // translucency
  half3 translucency;
  
  half4 litVector = lit( dotNL, dotHN, PSC_Specular.a );
  half coefDiffuse = litVector.y;
  half coefSpecular = litVector.z;
  coefSpecular = min( coefSpecular, 1.0h ); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
  half coefDiffuseBack = saturate( -dotNL ); // inverse direction light coef
  half3 colorBlend;
  half3 colorAdd;
 
// CROWN
if( crown )
{
  indirectLight = GetIndirectLightLerp( skyFactor ) * crownCenterTermA;  
  half mainLightCoef = saturate( dotNL * ppBleedMUL + ppBleedADD );
  directLight = PSC_Diffuse.rgb * mainLightCoef * diffuseAtten * shadowCoef;
  
  if( simple ) {
    specularDirLight = 0;
  } else {
    specularDirLight = PSC_Specular.rgb * coefSpecular * fresnel * shadowCoef * ambiOcc;
  }
  
  // TRANSLUCENCY
  half translucencyPP = saturate( dotNL * ppTranslucencyMul + ppTranslucencyAdd );
  half3 translucencyMainLight = translucencyPP * diffuseAtten * PSC_Diffuse.rgb;
  half3 translucencySky = GetIndirectLightLerp( skyFactorI ) * crownCenterTermA;
  /*if( simple ) {
    translucency = 0;
  } else {*/
    translucency = ( translucencyMainLight * shadowCoef * crownTranslucencyColor2.rgb + translucencySky * crownTranslucencyColor1.rgb )
    * colorFinal.rgb;
  //}
          
  // specular ambient
  half3 reflVec = 2.0h * dotCN * normal - dirToCamL;
  half skySpecFactor = 0.5 * dot( upVec, reflVec ) + 0.5;
  if( simple ) {
    specularAmbi = PSC_AE.rgb * specularAmbiColor.rgb * crownCenterTermA * 0.1; // 
  } else {
    specularAmbi = GetIndirectLightLerp( skySpecFactor )
      * specularAmbiColor.rgb * fresnel * crownCenterTermA; // * specularMask * ambientOcclusion * crownCenterAtt;
  }
  if (thermal)
  {
    colorBlend = colorFinal.rgb;
    colorAdd = 0;
  }
  else
  {
    colorBlend = ( sumVertLight + indirectLight + directLight ) * colorFinal.rgb;
    colorAdd =  translucency.rgb + specularDirLight.rgb + specularAmbi.rgb;
  }
  //colorAdd = translucency.rgb;
} 
// TRUNK
else {
  indirectLight = (GetIndirectLightLerp( skyFactor ) + PSC_LDirectionGround_DiffuseBack * coefDiffuseBack )* ambiOcc;
  half mainLightCoef = saturate( dotNL );
  directLight = PSC_Diffuse.rgb * mainLightCoef * shadowCoef;
  
  if( simple ) {
    specularDirLight = 0;
  } else {
    specularDirLight = PSC_Specular.rgb * coefSpecular * fresnel * shadowCoef;
  }
  
  // specular ambient
  half3 reflVec = 2.0h * dotCN * normal - dirToCamL;
  half skySpecFactor = saturate( 0.5 * dot( upVec, reflVec ) + 0.5 );
  if( simple ) {
    specularAmbi = PSC_AE.rgb * specularAmbiColor.rgb * 0.1;
  } else {
    specularAmbi = GetIndirectLightLerp( skySpecFactor )
      * specularAmbiColor.rgb * fresnel; // * specularMask * ambientOcclusion * crownCenterAtt;
  }

  if (thermal)
  {
    colorBlend = colorFinal.rgb;
    colorAdd = ambiOcc * 0.15;
  }
  else
  {
    colorBlend = ( sumVertLight + indirectLight + directLight ) * colorFinal.rgb;
    colorAdd = ( specularDirLight.rgb + specularAmbi.rgb ) * ambiOcc;
  }
}

  final.rgb = colorBlend.rgb + colorAdd.rgb;
  final.rgb = ApplyFog( final.rgb, i.tPSFog );
  s.colorFinal = final;
}

FOut PFDetailMacroAS(SDetailMacroAS input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLightAS(
    input.ambientColor,
    half3(0, 0, 0),
    input.x_x_nDotU_nDotL,
    tex2D(sampler3, input.tMacroMap_ShadowMap.wzyx));

  // Calculate the color
  fout.color = GetColorDiffuseDetailMacroAS(
    tex2D0(input.tDiffuseMap_DetailMap.xy),
    tex2D(sampler1, input.tDiffuseMap_DetailMap.wzyx),
    tex2D(sampler2, input.tMacroMap_ShadowMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFDetailSpecularAlphaMacroAS(SDetailSpecularAlphaMacroAS input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLightAS(
    input.ambientColor,
    input.specularColor_landShadow.rgb,
    input.x_x_nDotU_nDotL,
    tex2D(sampler3, input.tMacroMap_ShadowMap.wzyx));

  /*
  Not transcribed the handling of alpha from the original shader:
  mov_x2 r1, v1
  dp3_x2 r1, r1, r1
  */

  // Calculate the color
  fout.color = GetColorDiffuseDetailMacroAS(
    tex2D0(input.tDiffuseMap_DetailMap.xy),
    tex2D(sampler1, input.tDiffuseMap_DetailMap.wzyx),
    tex2D(sampler2, input.tMacroMap_ShadowMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMap(SNormalMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Get the irradiance data
  half4 normal = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half nDotL;
  half nDotU;
  half4 irradiance = GetIrradianceData(input.tLightLocal.xyz, input.tUpLocal.xyz, input.tHalfway.xyz, normal, sampler3, nDotL, nDotU);

  // Calculate the lights
  fout.light = GetLightIrradiance(input.ambientColor, irradiance, normal.a, nDotL, nDotU);

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = nDotL;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapDetailMacroASSpecularMap(SNormalMapDetailMacroASSpecularMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularAS(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx),
    tex2D(sampler4, input.tShadowMap_SpecularMap.xy),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetailMacroAS(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_MacroMap.xy),
    tex2D(sampler3, input.tDetailMap_MacroMap.wzyx),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapDetailMacroASSpecularDIMap(SNormalMapDetailMacroASSpecularDIMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularDIAS(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx),
    tex2D(sampler4, input.tShadowMap_SpecularMap.xy),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetailMacroAS(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_MacroMap.xy),
    tex2D(sampler3, input.tDetailMap_MacroMap.wzyx),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapDetailSpecularMap(SNormalMapDetailSpecularMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecular(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler3, input.tDetailMap_SpecularMap.wzyx),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_SpecularMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapDetailSpecularDIMap(SNormalMapDetailSpecularDIMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularDI(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler3, input.tDetailMap_SpecularMap.wzyx),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_SpecularMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapMacroAS(SNormalMapMacroAS input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Get the irradiance data
  half4 normal = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half nDotL;
  half nDotU;
  half4 irradiance = GetIrradianceData(input.tLightLocal.xyz, input.tUpLocal.xyz, input.tHalfway.xyz, normal, sampler3, nDotL, nDotU);

  // Calculate the lights
  fout.light = GetLightIrradiance(input.ambientColor, irradiance, normal.a, nDotL, nDotU);

  // Calculate the color
  fout.color = GetColorDiffuseMacroAS(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tMacroMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = nDotL;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapMacroASSpecularMap(SNormalMapMacroASSpecularMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularAS(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler4, input.tSpecularMap.xy),
    tex2D(sampler3, input.tMacroMap_ShadowMap.wzyx),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseMacroAS(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tMacroMap_ShadowMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapMacroASSpecularDIMap(SNormalMapMacroASSpecularDIMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularDIAS(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler4, input.tSpecularMap.xy),
    tex2D(sampler3, input.tMacroMap_ShadowMap.wzyx),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseMacroAS(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tMacroMap_ShadowMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapSpecularMap(SNormalMapSpecularMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecular(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler2, input.tSpecularMap.xy),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFNormalMapSpecularDIMap(SNormalMapSpecularDIMap input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularDI(input.ambientColor,
    tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tHalfway.xyz, input.tUpLocal.xyz,
    tex2D(sampler2, input.tSpecularMap.xy),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuse(
    tex2D0(input.tDiffuseMap_NormalMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFSpecularNormalMapDiffuse(SSpecularNormalMapDiffuse input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormal(input.ambientColor, input.specularColor_landShadow.rgb,
    tex2D(sampler1, input.tColorMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tUpLocal.xyz, coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tColorMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap.xy),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

FOut PFSpecularNormalMapDiffuseMacroAS(SSpecularNormalMapDiffuseMacroAS input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalAS(input.ambientColor, input.specularColor_landShadow.rgb,
    tex2D(sampler1, input.tColorMap_NormalMap.wzyx), input.tLightLocal.xyz, input.tUpLocal.xyz,
    tex2D(sampler4, input.tShadowMap.xy),
    coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetailMacroAS(
    tex2D0(input.tColorMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_MacroMap.xy),
    tex2D(sampler3, input.tDetailMap_MacroMap.wzyx),
    input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

//////////////////////////////////////////////

#define HeightFieldScale +0.03

/// see also landImpl.hpp
static const int LandMatStageSat = 0;
static const int LandMatStageMask = 1;
static const int LandMatStageMidDet = 2;

static const int LandMatStageLayers = 3;
static const int LandMatStagePerLayer = 2;
static const int LandMatNoInStage = 0;
static const int LandMatDtInStage = 1;
static const int LandMatLayerCount = 6;
/// satellite normal map stage
static const int LandMatStageSatNorm = 14;

#if defined(_XBOX) || _DEBUG>1 // with /Od full shaders are over 512 instructions, preventing debugging on DX9 HW
#define RAYCAST 0
#else
#define RAYCAST 1
#endif

#if RAYCAST

/**
When using switch inside of the inner loop, we get much shorter shaders (much less total instructions), however there needs to be a dynamic branch
inside of each loop, which might be slower.

Note: Experiments on X1950 did not confirm any measurable performance difference
*/
#define INNER_SWITCH 1

half SampleHeightField(const int stage, float2 dtMap, float mipLevel)
{
  // same differential in both x and y, different for u,v
  //return tex2Dgrad(samplers[stage],dtMap,rayDStep,rayDStep).r;
  //return tex2Dgrad(samplers[stage],dtMap,dx,dy).r;
  return tex2Dlod(samplers[stage],float4(dtMap,0,mipLevel)).r;
  //return 0;
}

half3 HeightFieldCastRay(float3 ray, const half layerIndex, float2 dtMap, float2 dx, float2 dy, half heightAlpha, half heightFarAlpha, half noMapSizeLog2)
{
  int nSteps = 8;
  
  const float stepScale = 0.1;
  float3 step = normalize(ray)/nSteps*stepScale;
  
  float stepSize = stepScale/nSteps;
  
  float3 offset = 0;
  half lastDepth = 0;
  
  // compute which LOD (or dx/dy) is needed
  // step.x is the du/dx, step.y is the dv/dx
  // to prevent aliasing reliable we pretend a little bit higher step
  // we do not use dx/dy, as it has no relation to how we are sampling the height map at all
  float2 rayDStep = step.xy*1.5;
  
  //float rayMip = 0;
  float rayMip = noMapSizeLog2 + log2( dot(rayDStep,rayDStep) )*0.5;
  //float rayMip = 0 + log2( dot(rayDStep,rayDStep) )*0.5;
  //float rayMip = 10 + log2( dot(rayDStep,rayDStep) )*0.5;
  
  [loop] for (int s=0; s<nSteps; s++)
  {
    half heightOffset;
    #if INNER_SWITCH
      if (layerIndex<0.5) heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage,dtMap-offset.xy,rayMip);
      else if (layerIndex<1.5) heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*1+LandMatNoInStage,dtMap-offset.xy,rayMip);
      else if (layerIndex<2.5) heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*2+LandMatNoInStage,dtMap-offset.xy,rayMip);
      else if (layerIndex<3.5) heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*3+LandMatNoInStage,dtMap-offset.xy,rayMip);
      else if (layerIndex<4.5) heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*4+LandMatNoInStage,dtMap-offset.xy,rayMip);
      else /*if (layerIndex<5.5)*/ heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*5+LandMatNoInStage,dtMap-offset.xy,rayMip);
    #else
      heightOffset = SampleHeightField(LandMatStageLayers+LandMatStagePerLayer*layerIndex+LandMatNoInStage,dtMap-offset.xy,dx,dy,rayDStep);
    #endif

    // we want heightOffset 1 to mean on surface (black is deep, white is shallow)
    half depth = lerp(0.5*HeightFieldScale,(1-heightOffset)*HeightFieldScale,heightAlpha)*heightFarAlpha;
    
    if (depth<=offset.z)
    {
      // intersection with interpolation from previous sample
      //return offset;
      float3 lastOffset = offset-step;
      half deltaC = offset.z-depth;
      float deltaL = lastOffset.z-lastDepth;
      half denom = deltaC-deltaL;
      half3 nom = lastOffset*deltaC-offset*deltaL;
      if (denom!=0) offset = nom/denom; // avoid division by zero
      return offset;
    }
    
    lastDepth = depth;
    
    offset += step;
  }
  // last offset was never sampled - we cannot return it as it might be under the surface
  // we need to return a previous sample which was verified to be OK.
  // as an alternative we might extrapolate from the last two samples
  return offset-step;
}

half HeightFieldCastRayShadow(half3 ray, float3 pos, const half layerIndex, float2 dtMap, float2 dx, float2 dy, half heightAlpha, half heightFarAlpha, half noMapSizeLog2)
{
  int nSteps = 6;
  
  const half stepScale = 0.10;
  half3 step = normalize(ray)/nSteps*stepScale;
  // first sample is not necessary - depth in it should always be zero, we can safely skip it
  half3 offset = pos + step;
  half lightThrough = 1;
  
  half distCoef = 1;
  
  half2 rayDStep = step.xy*1.5;
  float rayMip = noMapSizeLog2 + log2( dot(rayDStep,rayDStep) )*0.5;
  [loop] for (int s=0; s<nSteps-1; s++)
  {
    half heightOffset;
    #if INNER_SWITCH

      if (layerIndex<0.5) heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
      else if (layerIndex<1.5) heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*1+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
      else if (layerIndex<2.5) heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*2+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
      else if (layerIndex<3.5) heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*3+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
      else if (layerIndex<4.5) heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*4+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
      else /*if (layerIndex<5.5)*/ heightOffset = tex2Dlod(samplers[LandMatStageLayers+LandMatStagePerLayer*5+LandMatNoInStage],float4(dtMap-offset.xy,0,rayMip)).r;
    #else
      heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*layerIndex+LandMatNoInStage],dtMap-offset.xy,dx,dy).r;
    #endif

    
    // we want heightOffset 1 to mean on surface (black is deep, white is shallow)
    half depth = lerp(0.5*HeightFieldScale,(1-heightOffset)*HeightFieldScale,heightAlpha)*heightFarAlpha;
    
    // how deep is the ray under the surface
    half under = saturate((offset.z-depth)*300);
    
    // the further away we are from the shadow caster, the weaker we want the shadow
    lightThrough = min(lightThrough,1-under*distCoef);
    
    distCoef -= 1.0/nSteps;
    
    offset += step;
  }
  //return 1;
  return lightThrough;
}

#endif

#if 1
/// represent 8 values by one object
struct Half8
{
  half4 lo;
  half4 hi;
};
#define Half8Zeros {0,0,0,0,0,0,0,0}

void MulHalf8(inout Half8 x, half f)
{
  x.lo *= f;
  x.hi *= f;
}

void SetHalf8(inout Half8 x, int index, half value)
{
  if (index>=4) x.hi[index-4] = value;
  else x.lo[index] = value;
}
half GetHalf8(Half8 x, int index)
{
  if (index>=4) return x.hi[index-4];
  return x.lo[index];
}

#else
/// keep the same interface as Half8, but only for 4 layers
struct Half8
{
  half4 lo;
};
#define Half8Zeros {0,0,0,0}

void MulHalf8(inout Half8 x, half f)
{
  x.lo *= f;
}

void SetHalf8(inout Half8 x, int index, half value)
{
  x.lo[index] = value;
}
half GetHalf8(Half8 x, int index)
{
  return x.lo[index];
}
#endif

void NormalizeST(float3 middleST, float3 normal, out float3 outS, out float3 outT)
{
  // Normalize the middleST
  float t = dot(middleST,normal);
  float3 normalizedMiddleST = normalize(middleST - normal*t);

  // Get vector orthogonal to the middleST
  float3 orthoNormalizedMiddleST = cross(normalizedMiddleST,normal);

  // Get outS and outT from the middle of the middleST and orthoNormalizedMiddleST
  // Since the source vectors are orthonormal, the normalization of the destination is achieved
  // by dividing by sqrt(2)
  const float inv_sqrt_2 = 0.70710678119;
  outS = (normalizedMiddleST+orthoNormalizedMiddleST) * inv_sqrt_2;
  outT = (normalizedMiddleST-orthoNormalizedMiddleST) * inv_sqrt_2;
}


//-----------------------------------------------------------------------------
/*!
compute tangent space and return it as matrix 3x3 with rows
| tangent	|
| binormal	|
| normal		|
*/
//-----------------------------------------------------------------------------
float3x3 ComputeTangentSpace(float3 normal)
{	
	float3 binormal = normalize(float3(0.0f, normal.z + 0.001, -normal.y));
		
	float3 tangent = cross(normal, binormal );
	binormal = cross (normal, tangent);
	
	return float3x3(-tangent, -binormal, normal);
}


static const int ColorsCount = 2;
static const half3 Colors[ColorsCount] = {
  1.0, 0.75, 0.5,
  0.7, 0.7, 0.9
};

half3 CalculateLightColor(half clr)
{
  /*
  int idx = clamp(clr * ColorsCount, 0, ColorsCount - 1);
  return lerp(half3(0,0,0), Colors[idx], frac(clr * ColorsCount));//lerp(Colors[idx], half3(1,1,1), clr * clr) * clr;
  */
  return clr * lerp(Colors[1], Colors[0], clr) * PSC_NightEmissiveCoef.xyz;
}

#define HELISIM_NIGHT_LIGHTING 1
#define HELISIM_WATER_LAYER 1

Light GetLightTerrain(half3 position, half3 normal, half3 detailNormal, half3 lightDir, half3 eyeDir,
                      half3 ambientColor, half3 specularColor,
                      half indirectLight, half selfshadow, half alpha)
{
  Light light = (Light) 0;

  half nDotL = dot(normal, lightDir);
  half diffuse = max(nDotL, 0) * selfshadow;
  half diffuseBack = max(-nDotL, 0) * selfshadow;

  // Fill out the Light structure
  half3 upLocal = half3(0, 0, 1); // We can consider the normal map on ground is mapped always the similar way - down the ground. That's why this is roughly correct

  light.direct = diffuse * PSC_Diffuse;
  light.indirect = GetIndirectLight(dot(normal, upLocal)) + ambientColor.rgb
    + PointSpotLighting(position, normal) + PSC_LDirectionGround_DiffuseBack * diffuseBack
#if HELISIM_NIGHT_LIGHTING
    + CalculateLightColor(indirectLight)
#endif
    ;

#if HELISIM_WATER_LAYER
  if (alpha > 0.01)
  {
    half3 halfway = normalize((lightDir + eyeDir) / 2);
    half specular = lit(nDotL, dot(detailNormal, halfway), 96).z;
    light.specular = alpha * specular * PSC_Diffuse.rgb;
  }
  else
#endif
    light.specular = specularColor;

  light.diffuseShadowCoef = 1;

  return light;
}

// @param useSatNorm .. 0 = do not use satellite normal map, 1 = use satellite normal map
FOut PFTerrainBodyX(STerrain input, const bool layers[6], bool simple, bool grass, bool sbShadows, bool alphaOnly, bool thermal, int useSatNorm, bool water)
{
  FOut fout = InitFOut();

#if _SM2
  simple = true;
#endif

  // Sample satellite and mask maps
  float2 satMask = input.tSatAndMask_GrassMap.xy;
  half3 satTexture   = tex2D0(satMask);
  half4 maskTexture  = tex2D(sampler1,satMask);
  half waterAlpha = 0;
  int waterLayerIdx = -1;

  Half8 alphas = Half8Zeros;
  {
    int passIndex = 0;
    half first = 1;
    [unroll] for (int i=0; i<LandMatLayerCount; i++)
    {
      if (layers[i])
      {
        // read constant telling us if the layer is on or off
        half layerOn = PSC_Layers[i].x;
        half alpha;
        // read the corresponding mask channel
        if (passIndex>0)
        {
          if (i<3)
          {
            // ABC style weights
            alpha = maskTexture[i-1];
          }
          else
          {
            // overall weight for the whole XYZ layer
            half cAlpha = maskTexture[3-1];
            // XYZ style weights
            // when texture alpha is 1, we want layer 0 to be used
            half a = 1-maskTexture[3];
            // weight for layer 1 grows between a = 0 .. 0.5 
            // weight for layer 2 grows between a = 0.5 .. 1
            half weightsXYZ[3] = {1, saturate(a*2), saturate((a-0.5)*2) };
            // compute overall weight
            alpha = weightsXYZ[i-3]*cAlpha;
          }
        }
        else alpha = 1;

        if (layerOn < -0.5)
        {
          waterAlpha = alpha;
          waterLayerIdx = passIndex;
          layerOn = 0;
        }

        // we want the first layer which is really used to be always opaque
        // on the other hand, if the channel is off, we want the alpha to be fully transparent
        alpha = max(alpha,first)*layerOn;
        // if the layer was used, we want to reset the "first"
        first = min(first,1-layerOn);
        
        // all alphas blended so far need to be reduced by 1-alpha
        MulHalf8(alphas,1-alpha);
        SetHalf8(alphas,passIndex,alpha);
        // proceed to the next stage
        passIndex++;
      }
    }
  }
  
  // self shadowing computed by occlusion mapping
  half selfshadow = 1;

  float3 offset = 0;
  float2 coMap = input.tColorMap_NormalMap.xy;
  float2 dtMap = input.tDetailMap_SpecularMap.xy;
  // noMap most often shares mapping with a detail map
  //float2 noMap = input.tColorMap_NormalMap.wz;

  //dtMap = frac(dtMap);

  #if 1
    // first of all select dominant layer
    // this is used for ray-casting, as ray-casting with sampling all layers and mask is way too expensive
    
    half heightLayer = 0; // sampler containing the height map
    half noMapSizeLog2 = 0;
    
    //
    half heightAlpha = 0; // height map influence - used for transitions
    half heightFarAlpha =0; // used for transition into normal maps used far away
    {
      int passIndex = 0;
      // find last stage with effective alpha>0.5
      // how much of alpha is covered by the layers which were already processed
      half maxAlpha = 0;
        
      [unroll] for (int i=0; i<LandMatLayerCount; i++)
      {
        if (layers[i])
        {
          half effectiveAlpha = GetHalf8(alphas,i);
          if (effectiveAlpha>maxAlpha)
          {
            maxAlpha = effectiveAlpha;
            heightLayer = i;
            if (i<4) noMapSizeLog2 = PSC_NoTexSizeLog2[0][i];
            else noMapSizeLog2 = PSC_NoTexSizeLog2[1][i-4];
          }
          passIndex++;
        }
      }
      // map maxAlpha = 1 to transitionFactor = 1
      // map maxAlpha = 0.5 to transitionFactor = 0
      // this is too much conservative with more than two surfaces blending together
      // however is guarantees smooth transitions everywhere
      heightAlpha = max(maxAlpha*2-1,0);
      //heightAlpha = 1;
    }
  #endif
  
  // input.tEyeLocal.xyz - contains model space eye offseet
  // input.grassAlpha.xyz - contains model space normal
  
  // construct local (ST) space
  half3 localN = input.grassAlpha.xyz;

  
  
  // convert eyeLocal from model space to local space
  
//  half3 localS, localT;
//  NormalizeST(float3(1,0,1), localN, localT, localS);
//   half3 eyeLocal;
//  eyeLocal.x = dot(localS,input.tEyeLocal.xyz);
//   eyeLocal.y = dot(localN,input.tEyeLocal.xyz);
// eyeLocal.z = dot(localT,input.tEyeLocal.xyz);

  //MIVA
  float3 eyeWorld = normalize(input.tEyeLocal.xyz);
  float3 parnormal = normalize(localN.xzy + float3(0,0,0.5));
  float3x3 tspace = ComputeTangentSpace(parnormal);
  float3 eyeLocal = mul(tspace, eyeWorld);


  //ondrovo
  //half3 eyeLocal = input.tEyeLocal.xyz;
  //eyeLocal = normalize(eyeLocal);

  // Get the light in local vertex space
  half3 lightDir = normalize(input.tLightLocal.xyz);
  
  // avoid tEyeLocal being under the surface, which might happen as a result of ST space interpolation
  // eyeLocal.z = (eyeLocal.z + 1.0)*0.5;//max(eyeLocal.z,0.2);
  
#if RAYCAST
  // we want parallax mapping only with Shading Detail Normal or better
  if (!simple && !grass)
  {
    //[branch]
    //if (PSC_ShadingDetail[1])
    //if (false)
    {
      float2 dx = ddx(dtMap), dy = ddy(dtMap);
      
      // read (and average) all height maps for parallax mapping
      // perform multiple samples to find a first intersection

      //float2 dx = 0, dy = 0;
    
      // Find min of change in u and v across quad: compute du and dv magnitude across quad
      half2 dTexCoords = dx * dx + dy * dy;

      // Standard mip-mapping uses max here
      half minTexCoordDelta = max( dTexCoords.x, dTexCoords.y );

      // Compute the current mip level  (* 0.5 is effectively computing a square root before )
      half mipLevel = 0.5 * log2( minTexCoordDelta );
      
      // once mip level reaches a threshold, start transition to normal mapping
      // what we get is a "relative mip", where 0 = whole u/v range mapped to 1 texel
      // mipLevel = -10 means resolution 1024x1024 would be desired
      // endFade = -10 means transition starts very close to the camera
      // endFade = -6 means transition starts quite far from the camera
      
      const half startFade = -6;
      const half endFade = -8;
      
      if (mipLevel<startFade)
      {
        half lodFactor = 1;
        if (mipLevel>endFade)
        {
          lodFactor = 1-(mipLevel-endFade)/(startFade-endFade);
        }
        heightFarAlpha = lodFactor;
        #if INNER_SWITCH
          offset = HeightFieldCastRay(eyeLocal,heightLayer,dtMap,dx,dy,heightAlpha,heightFarAlpha,noMapSizeLog2);
          selfshadow = HeightFieldCastRayShadow(-lightDir,offset,heightLayer,dtMap,dx,dy,heightAlpha,heightFarAlpha,noMapSizeLog2);
        #else
          [branch] switch (heightLayer)
          {
            default:
              offset = HeightFieldCastRay(eyeLocal,0,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,0,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
            case 1:
              offset = HeightFieldCastRay(eyeLocal,1,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,1,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
            case 2:
              offset = HeightFieldCastRay(eyeLocal,2,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,2,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
            case 3:
              offset = HeightFieldCastRay(eyeLocal,3,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,3,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
            case 4:
              offset = HeightFieldCastRay(eyeLocal,4,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,4,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
            case 5:
              offset = HeightFieldCastRay(eyeLocal,5,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              selfshadow = HeightFieldCastRayShadow(-lightDir,offset,5,dtMap,dx,dy,heightAlpha,heightFarAlpha);
              break;
          }
        #endif
        
        //selfshadow *= lodFactor;
        
        dtMap -= offset.xy;
		  //fout.color = half4(5*abs(offset.x), 5*abs(offset.y), 0, 1);
      }
    }
    
    //selfshadow = saturate(startFade-mipLevel);

  }
#else
  if (!simple && !grass)
  {
    // read dominant height map for parallax mapping
    half heightOffset;
    
    float2 dx = ddx(dtMap), dy = ddy(dtMap);
    
    [branch] switch (heightLayer)
    {
      default: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*0+LandMatNoInStage],dtMap,dx,dy).r; break;
      case 1: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*1+LandMatNoInStage],dtMap,dx,dy).r; break;
      case 2: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*2+LandMatNoInStage],dtMap,dx,dy).r; break;
      case 3: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*3+LandMatNoInStage],dtMap,dx,dy).r; break;
      case 4: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*4+LandMatNoInStage],dtMap,dx,dy).r; break;
      case 5: heightOffset = tex2Dgrad(samplers[LandMatStageLayers+LandMatStagePerLayer*5+LandMatNoInStage],dtMap,dx,dy).r; break;
    }
    // in the map: 0 = deep, 1 = shallow
    // convert to 1 = deep, 0 = shallow. At the same time apply transition.
    offset = eyeLocal*(1-heightOffset)*heightFarAlpha*HeightFieldScale;
    dtMap -= offset.xy;
  }
#endif

  float2 dx = ddx(dtMap), dy = ddy(dtMap);
  float2 dxCo = ddx(coMap), dyCo = ddy(coMap);
  
  float2 noMap;
  float2 dxNo,dyNo;
  if (!grass)
  {
    noMap = dtMap;
    dxNo = dx, dyNo = dy;
  }
  else
  {
    // grass layer needs a specific "normal map stage" mapping
    noMap = input.tSatAndMask_GrassMap.wz;
    //satMask*SegGridScale.xx+SegGridScale.yy;
    dxNo = ddx(noMap), dyNo = ddy(noMap);
  }

  // Get first layer
  half3 detTextureAvg = 0;
  half3 detTexture = 0;
  half4 normalMap=0;

  half3 primTexture = tex2D(samplers[LandMatStageMidDet],coMap);

  {
    int passIndex=0;  
    // coordinates for sampling smallest mipmap of the detail texture
    float4 dtLod = float4(dtMap,0,20);
    [unroll] for (int i=0; i<LandMatLayerCount; i++)
    {
      if (layers[i])
      {
        half alpha = GetHalf8(alphas,passIndex);
        // while dynamic branching should be able to reduce texture loads significantly
        // in practice it seems the difference is marginal
        // Avoid tex2Dgrad as it is very slow on nVidia HW. Explanation from Kirill Dmitriev mailto:KDmitriev@nvidia.com
        
        /*
        Quad has four pixels. DDX between the upper two may return different value than DDX between the lower two. And
        the same for DDY between left pair and right pair. As I understand this is the reason why different pixels in
        the quad may fetch from different mipmap levels when using tex2Dgrad. tex2D uses something like an average between
        two DDXs and between two DDYs, thus it ends up with 4 pixels landing on the same mip level - which is faster.
        */
        
        #if !_SM2
        // dynamic branching (SM3 only) would be a significant optimization here
        // however we cannot use it, because noMap/dxMap are computed
        //[branch] if (alpha>0)
        #endif
        {
          int base = LandMatStageLayers+LandMatStagePerLayer*passIndex;
          normalMap   += tex2D(samplers[base+LandMatNoInStage],noMap)*alpha;
          detTexture  += tex2D(samplers[base+LandMatDtInStage],dtMap)*alpha;
          
#if _SM2
          detTextureAvg += tex2Dbias(samplers[base+LandMatDtInStage],dtLod)*alpha;
#else
          detTextureAvg += tex2Dlod(samplers[base+LandMatDtInStage],dtLod)*alpha;
#endif
        }
        passIndex++;
      }
    }
  }
  

  // now adjust the detTexture color to match satellite map
  //detTexture = min(detTexture*(satTexture/detTextureAvg),1);
  // See COLOR_MODIFICATION
#if _SM2
  if (!thermal)
#endif
  {
    half3 adjust = (satTexture/detTextureAvg);
    adjust = min(2,max(adjust,0.5));
    detTexture = detTexture*adjust;
    //detTexture = detTextureAvg;

    //detTexture = alphas;
  }
  
  /**/
  // lerp - detail map / satellite map
  // distance is given in ambientColor.a
  // Calculate the texel color
  half3 colorTexel;
  half diffuse;
  half diffuseBack;
  half indirectLight = 0;
  
  half3 normal;
  //primTexture = 0.5;
  if (grass)
  {
    half perlin = tex2D(samplers[LandMatStageLayers+LandMatDtInStage],noMap).a;
    // grass is rendered for middle distance
    // no detail textures needed for the grass
    //colorTexel = lerp(satTexture*primTexture*2,normalMap.rgb,input.grassAlpha.XXX);
    colorTexel = satTexture*primTexture*2;

    normal = half3(0,0,1);
    // normal map has different meaning - alpha used only for alpha testing
    // clip only needed during z-priming (i.e. when rendering alpha only)
    if (alphaOnly)
    {
      clip(normalMap.a*input.grassAlpha.w+perlin-1);
    }
    //clip(-1);

    // This code will disable grass layer for thermal imaging mode (grass layer is not very important in thermal vision anyway)
    if (thermal)
    {
      colorTexel = half3(0, 0, 0);
      clip(-1);
    }
  }
  else if (simple)
  {
#if _SM2
    if (thermal)
    {
      colorTexel = satTexture;
    }
    else
#endif
    {
      half detFactor = input.ambientColor.a;
      colorTexel = lerp(satTexture,detTexture,detFactor);
    }
    normal = half3(0,0,1);
  }
  else
  {
    // coef for satellite / detail blending (based on distance)
    half satDetCoef = input.ambientColor.a;

    colorTexel = lerp(satTexture*primTexture*2,detTexture,satDetCoef);

    // Get the normal from normal map for detail textures
    // we know the normal map is DXT5 compressed (hq, vhq or nopx)
    half2 normalXY = half2(1 - normalMap.a,normalMap.g)*2-1;
	// Sometimes normal is (0,0,0,0), so we have to be careful (otherwise sqrtf would crash the shader)
    half factor = max(1-dot(normalXY,normalXY), 0);
    half3 normalDetail = half3(normalXY,sqrt(factor));

    // get the normal map for satellite (and uncompress from DXT5, should also work with DXT1)
    half4 normalMapSat = tex2D(samplerSN,satMask);
    normalXY = (normalMapSat.rg + half2(1 - normalMapSat.a,0))*2-1;
	  // Sometimes normal is (0,0,0,0), so we have to be careful (otherwise sqrtf would crash the shader)
    factor = max(1-dot(normalXY,normalXY), 0);
    half3 normalSat = half3(normalXY,sqrt(factor));

    indirectLight = normalMapSat.b;

    // compute coeficient for satellite normal map / detail normal map blending - based on distance from eye
    // PSC_TerrainSatNormDist.x = distance where detail normal starts to fade to sat. normal
    // PSC_TerrainSatNormDist.y = fading length
    half eyeDistance = length(input.tEyeLocal.xyz);
    float coef = saturate((eyeDistance - PSC_TerrainSatNormDist.x)/ PSC_TerrainSatNormDist.y);

    // PSC_Layers[LandMatLayerCount-1] contains user constant used for toggling satellite normal on and off (see SetPixelShaderConstantLayers)
    half useSatNormCoef = useSatNorm * PSC_Layers[LandMatLayerCount-1].x;
    coef = coef * useSatNormCoef;

    // create transformation matrix - we want to transform detail map normal to the space defined by satellite map normal
    float3x3 transformMat;
    transformMat[0] = float3(1,0,0);
    transformMat[2] = normalSat;
    transformMat[1] = cross(transformMat[2], transformMat[0]);
    transformMat[0] = cross(transformMat[1], transformMat[2]);

    half3 normalDetailWithSat = mul(normalDetail, transformMat);

    // PSC_TerrainSatNormDist.z = 1.0f if satellite map on detail maps should be used, 0 otherwise
    float satNormalOnDetailCoef = useSatNormCoef * PSC_TerrainSatNormDist.z;
    normalDetail = lerp(normalDetail, normalDetailWithSat , satNormalOnDetailCoef);

    // final normal
    normal = lerp(normalDetail, normalSat, coef);

    // renormalize after lerp
    normalize(normal);
  }

  // Calculate the diffuse coefficient and initialize the ordinary terrain lighting.
  half nDotL = dot(normal, lightDir);
  diffuse = max(nDotL, 0) * selfshadow;
  diffuseBack = max(-nDotL, 0) * selfshadow;

  half3 upLocal = half3(0, 0, 1);
  fout.light.indirect = GetIndirectLight(dot(normal, upLocal)) +
    input.ambientColor.rgb +
    // Transform the mapped normal into the object-space basis used by local lights.
    PointSpotLighting(input.tPSFog.xyz, mul(tspace, normal.xzy)) +
    PSC_LDirectionGround_DiffuseBack * diffuseBack;
  fout.light.direct = diffuse * PSC_Diffuse;
  fout.light.specular = input.specularColor.rgb;
  fout.light.specularEnv = 0;
  fout.light.diffuseShadowCoef = 1;

  if (water)
  {
#if HELISIM_NIGHT_LIGHTING
    fout.light.indirect += CalculateLightColor(indirectLight);
#endif

#if HELISIM_WATER_LAYER
    if (waterAlpha > 0.001)
    {
      half3 waterNormal = 0;
      half3 envColor = 0;

      [unroll] for (int i=0; i<LandMatLayerCount; i++)
      {
        if (layers[i] && i == waterLayerIdx)
        {
          int base = LandMatStageLayers+LandMatStagePerLayer*i;
          waterNormal.xy = tex2D(samplers[base+LandMatNoInStage], noMap*0.05).ag;
        }
      }

      waterNormal.xy = half2(1 - waterNormal.x,waterNormal.y)*2-1;
      waterNormal.z = sqrt(max(0, 1-dot(waterNormal.xy, waterNormal.xy)));
      half3 upLocal = half3(0, 0, 1);
      half dCoef = saturate((length(input.tEyeLocal.xyz) -
        4*PSC_TerrainSatNormDist.x)/PSC_TerrainSatNormDist.y);
      waterNormal = lerp(waterNormal, upLocal, dCoef);

      [unroll] for (int i=0; i<LandMatLayerCount; i++)
      {
        if (layers[i] && i == waterLayerIdx)
        {
          int base = LandMatStageLayers+LandMatStagePerLayer*i;
          float2 r = reflect(eyeWorld, waterNormal).xy;
          envColor = tex2D(samplers[base+LandMatDtInStage],
            r.xy*half2(0.5,-0.5)+0.5);
        }
      }

      envColor = lerp(envColor, 0, dCoef);
      half3 halfway = normalize((lightDir + eyeWorld) / 2);
      half4 litCoef = lit(dot(lightDir, waterNormal),
        dot(waterNormal, halfway), 128) * PSC_Diffuse;
      fout.light.direct = litCoef.y;
      fout.light.specular = litCoef.z * waterAlpha * 0.4;
      fout.light.specularEnv = PSC_Diffuse * envColor * waterAlpha;
      colorTexel.rgb = lerp(colorTexel, satTexture, waterAlpha);
    }
#endif
  }
  
  // Calculate the color
  fout.color = half4(colorTexel, 1);

  // Calculate the fog
  fout.fog = input.tPSFog.w;

  // Calculate the land shadow
  // specularColor alpha contains depth under the shadow horizon
  // depth=0 -> shadow = 1
  // depth=1 -> shadow = 0
  fout.landShadow = saturate(0.5-input.specularColor.a*0.5)*0.8+0.2;

  // Initialize the halfCooling time
  // The closer is the base color to grass color, the smaller the halfCooling time is
  half3 grassColor = half3(0.0123, 0.0118, 0.0185);
  half3 grassDiff = (fout.color.rgb - grassColor);
  half3 grassDiff2 = grassDiff * grassDiff;
  half grassCoef = sqrt(grassDiff2.r + grassDiff2.g + grassDiff2.b);
  fout.airCoolingFactor = max(1.0f - grassCoef, 0); // _VBS3_TI

  // Remember the nDotL
  fout.nDotL = diffuse;

  /* // zeroing not needed - the caller will not use the results anyway
  if (alphaOnly)
  {
    // with alpha only the only thing we need from the shader is the clip (texkill)
    // we do not need any color to be returned
    fout.light.indirect = 0;
    fout.light.direct = 0;
    fout.light.specular = 0;
    fout.light.specularEnv = 0;
    fout.light.diffuseShadowCoef = 1;
    fout.color = 0;
    fout.fog = 0;
    fout.landShadow = 1;
    fout.halfCooling = 0;
    fout.htCoef = 0;
  }
  */

  return fout;
}


#define TERRAINLAYER_N(x,N) \
  FOut PFTerrain##N(STerrain input, bool sbShadows, bool thermal) \
  { \
  const bool layers[6]={(x%2)!=0, ((x/2)%2)!=0, ((x/4)%2)!=0, ((x/8)%2)!=0, ((x/16)%2)!=0, ((x/32)%2)!=0}; \
  return PFTerrainBodyX(input, layers, false, false, sbShadows, false, thermal, 0, false); \
} \
  FOut PFTerrainSimple##N(STerrain input, bool sbShadows, bool thermal) \
  { \
  const bool layers[6]={(x%2)!=0, ((x/2)%2)!=0, ((x/4)%2)!=0, ((x/8)%2)!=0, ((x/16)%2)!=0, ((x/32)%2)!=0}; \
  return PFTerrainBodyX(input, layers, true, false, sbShadows, false, thermal, 0, false); \
} \
  FOut PFTerrainGrass##N(STerrain input, bool sbShadows, bool thermal) \
  { \
  const bool layers[6]={(x%2)!=0, ((x/2)%2)!=0, ((x/4)%2)!=0, ((x/8)%2)!=0, ((x/16)%2)!=0, ((x/32)%2)!=0}; \
  return PFTerrainBodyX(input, layers, true, true, sbShadows, false, thermal, 0, false); \
} \

TERRAINLAYER_N(15,15)
#if _SM2
TERRAINLAYER_N(15,X)
#else
TERRAINLAYER_N(63,X)
#endif

// PFTerrainSN = Terrain with Satellite Normal map (SN)
//      - layers[5] is set to 0 - because the last layer is used for satellite normal map
FOut PFTerrainSNX(STerrain input, bool sbShadows, bool thermal) \
{ \
const bool layers[6]={1, 1, 1, 1, 1, 0}; \
return PFTerrainBodyX(input, layers, false, false, sbShadows, false, thermal, 1, true); \
} \

//////////////////////////////////////////////
// very similar to PFTerrain and PFNormalMapDetailSpecularMap

FOut PFRoad(SRoad input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  half3 upLocal = half3(0, 0, 1); // We can consider the normal map on roads is mapped always the similar way - down the ground. That's why this is roughly correct
  half coefDiffuse;
  half3 halfway = normalize(normalize(input.tEyeLocal.xyz)+input.tLightLocal.xyz);
  fout.light = GetLightNormalSpecular(
    input.ambientColor + PointSpotLighting(input.tPSFog.xyz, input.tNormalLocal.xyz),
    tex2D(sampler1, input.tColorMap_NormalMap.wz), 
    input.tLightLocal.xyz, halfway, upLocal,
    tex2D(sampler3, input.tDetailMap_SpecularMap.wz),
    coefDiffuse);
 
  // Calculate the color
  fout.color = GetColorDiffuseDetail(
    tex2D0(input.tColorMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_SpecularMap.xy),
    input.ambientColor.a, 0);

  // Remember the nDotL
  fout.nDotL = coefDiffuse;
  fout.airCoolingFactor = 0.2;

  // Calculate the fog
  fout.fog = input.tPSFog.w;

  // Calculate the land shadow
  // specularColor alpha contains depth under the shadow horizon
  // depth=0 -> shadow = 1
  // depth=1 -> shadow = 0
  fout.landShadow = saturate(0.5-input.specularColor.a*0.5)*0.8+0.2;

  // LUKE HACK
  //fout.light.indirect = half3(1,1,1);
  //fout.light.direct = 0;
  //fout.light.specular = 0;
  //fout.light.specularEnv = 0;
  //fout.light.diffuseShadowCoef = 1;
  ////fout.color = half4(satTexture.rgb, 1.0f);
  //fout.fog = 1;
  //fout.landShadow = 1;
  //fout.htCoef = 0;

  return fout;
}

FOut PFRoad2Pass(SRoad input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Kill output in case of alfa
  clip(tex2D0(input.tColorMap_NormalMap.xy).a - 0.5f);

  // Calculate the lights
  half coefDiffuse = max(dot(half3(0, 0, 1), input.tLightLocal.xyz), 0.0f);
  fout.light = GetLightSimple(half3(0, 0, 0), half3(0, 0, 0), half3(0, 0, 0));

  // Calculate the color
  fout.color = half4(0, 0, 0, 1);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  // Calculate the land shadow
  // specularColor alpha contains depth under the shadow horizon
  // depth=0 -> shadow = 1
  // depth=1 -> shadow = 0
  fout.landShadow = saturate(0.5-input.specularColor.a*0.5)*0.8+0.2;

  return fout;
}



//////////////////////////////////////////////

FOut PFGlass(SGlass input, bool sbShadows, bool thermal)
{
  // Calculate the O vector (the reflection vector)
  half3 o;
  {
    half3 v = normalize(-input.lwsPosition.xyz);
    o = input.lwsNormal.xyz * dot(input.lwsNormal.xyz, v) * 2.0f - v;
  }

  // Get the color from the environmental map
  //half3 envColor = tex2D(sampler2, o.xy * 0.5 + 0.5);
  half3 envColor = tex2D(sampler2, o.xy * float2(1, -1) * 0.5 + 0.5) * PSC_GlassEnvColor.rgb;

  // Get the fresel reflection coefficient
  half fresnelCoef = tex2D(sampler1, float2(dot(input.lwsNormal.xyz, o), 0.5)).a;

  FOut fout = InitFOut();

  // Sample the diffuse color
  half4 diffuseColor = tex2D0(input.tDiffuseMap.xy);
  clip(diffuseColor.a - 1.0f/255.0f); // Clip output in case of 100% alpha

  // Get the diffuse coefficient
  half coefDiffuse = max(input.x_x_nDotU_nDotL.w, 0);
  half coefDiffuseBack = max(-input.x_x_nDotU_nDotL.w, 0);

  // Calculate the lights
  fout.light.indirect = input.ambientColor + GetIndirectLight(input.x_x_nDotU_nDotL.z) + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack;
  fout.light.direct = /*PSC_DForced + Not required*/ PSC_Diffuse * coefDiffuse;
  half3 baseSpecular = (input.specularColor_landShadow.rgb + envColor * PSC_GlassMatSpecular * 2.0f) * fresnelCoef; // 2 is here because specular color is halved
  fout.light.specular = baseSpecular;
  fout.light.specularEnv = 0;
  fout.light.diffuseShadowCoef = 1;

  // Calculate the color
  fout.color = diffuseColor;
  //fout.color = half4(1,0,0,1);

  // In thermal vision the glass is almost opaque
  // _VBS3_TI
  if (thermal) fout.color.a = lerp(fout.color.a, 1.0f, 0.98f);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = input.x_x_nDotU_nDotL.w;

  return fout;
}

//////////////////////////////////////////////

FOut PFCraterX(SCrater input, int nCraters, bool sbShadows, bool thermal)
{
  // Sample the diffuse map
  half4 diffuseMap = tex2D0(input.tDiffuseMap.xy);

  //   // More sofistikated footprint - oval shape
  //   float3 closestVector = input.originalPosition.xyz - PSC_Crater[0].xyz;
  //   for (int i = 1; i < nCraters; i++)
  //   {
  //     float3 v = input.originalPosition.xyz - PSC_Crater[i].xyz;
  //     if (dot(v*v, 1) < dot(closestVector*closestVector, 1))
  //     {
  //       closestVector = v;
  //     }
  //   }
  //   float radius = 0.02f;
  //   float3 direction = float3(0, 1, 0);
  //   float stretch = dot(normalize(closestVector), direction);
  //   float coef = radius * (1 + abs(stretch*stretch*stretch)) * rsqrt(dot(closestVector, closestVector)) - 1;
  //   float icoef = (1 - saturate(coef));
  //   half wetFactor = (1 - icoef * icoef * icoef * icoef) * diffuseMap.a;



  // Find the closest crater distance weighted by the crater size
  float distance2 = 100.0f;
  float temperature = 0.0f;
#if !_SM2
  [unroll] for (int i = 0; i < nCraters; i++)
  {
    float3 v = PSC_Crater[i].xyz - input.originalPosition.xyz;
    float d2 = dot(v*v, PSC_Crater[i].www);
    if (d2 < distance2)
    {
      distance2 = d2;
      temperature = PSC_CraterTemp[i].x;
    }
  }
#endif

  // Calculate darkening factor
  float craterCoef = 0.9f;
  half wetFactor = (1.0f - min(distance2 * distance2, 1)) * diffuseMap.a * craterCoef;

  // Fill out the FOut structure
  FOut fout = InitFOut();

  // Calculate the lights (very simplified lighting for craters - input.originalPosition.w stores the diffuse coefficient)
  fout.light = GetLightSimple(PSC_GlassEnvColor.rgb, PSC_GlassEnvColor.rgb * input.originalPosition.w, half3(0, 0, 0));

  // Calculate the color
  fout.color.rgb =  half3(1, 1, 1);
  fout.color.w = wetFactor * PSC_GlassEnvColor.a;
  fout.color.w *= thermal ? temperature : 1.0;
  fout.addTemp = 10*temperature;

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = 1;

  return fout;
}

#define CRATER(x) \
FOut PFCrater##x(SCrater input, bool sbShadows, bool thermal) \
{ \
  return PFCraterX(input, x, sbShadows, thermal); \
} \

CRATER(1)
CRATER(2)
CRATER(3)
CRATER(4)
CRATER(5)
CRATER(6)
CRATER(7)
CRATER(8)
CRATER(9)
CRATER(10)
CRATER(11)
CRATER(12)
CRATER(13)
CRATER(14)

//////////////////////////////////////////////

#define MAX_HALF 65504.0

FOut PFSpriteSimple(SSprite input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    0);
  fout.light.indirect *= input.modulateColor;
  fout.light.direct *= input.modulateColor;

  // Calculate the z offset to perform the non-linear disappearing
  half4 tex0 = tex2D0(input.tDiffuseMap.xy);

  // Calculate the color
  fout.color = GetColorDiffuse(tex0,input.ambientColor.a * input.modulateColor.a, 0);
  fout.psBlend = true;
  if (fout.color.a<0)
  {
    fout.addBlend = true;
    fout.color.a = -fout.color.a;
  }
  
  // make sure the "color" can be represented as half with no overflow
  fout.color = min(fout.color,MAX_HALF);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = 1; // Sprite is a special object - it actually doesn't matter where the sun is shining from

  return fout;
}

//////////////////////////////////////////////

FOut PFSprite(SSprite input, bool sbShadows, bool thermal)
{
  // If we use SM2 and thermal vision, use PFSpriteSimple instead, as we run out of available instructions in that case
#if _SM2
  if (thermal) return PFSpriteSimple(input, sbShadows, thermal);
#endif

  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    0);
  fout.light.indirect *= input.modulateColor;
  fout.light.direct *= input.modulateColor;

  // Calculate the z offset to perform the non-linear disappearing
  half4 tex0 = tex2D0(input.tDiffuseMap.xy);

  // Get the depth buffer value in meters
  half z = 1.0f / tex2D(sampler1, input.tPosition.xy+(PSC_InvW_InvH_X_X.xy*0.5)).r;

  // Calculate the alpha coefficient (soft particles)
  //half zOffset = input.tPosition.w * (1.0f - tex0.a);
  //half alphaCoef = saturate((z - (input.tPosition.z + zOffset)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w * (1.0f - tex0.a)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w + input.tPosition.w*tex0.a)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w)) / input.tPosition.w + tex0.a);
  half alphaCoef = saturate((z - input.tPosition.z)/input.tPosition.w + tex0.a);

  // Calculate the color
  fout.color = GetColorDiffuse(tex0,input.ambientColor.a * input.modulateColor.a * alphaCoef, 0);

  fout.psBlend = true;
  if (fout.color.a<0)
  {
    fout.addBlend = true;
    fout.color.a = -fout.color.a;
  }

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = 1; // Sprite is a special object - it actually doesn't matter where the sun is shining from

  fout.airCoolingFactor = 0.0;

  // The closer is the base color to fire color, the higher the particle color is
  half3 fireColor = half3(1, 0.5, 0);
  half3 fireDiff = (fout.color.rgb - fireColor);
  half3 fireDiff2 = fireDiff * fireDiff;
  half fireCoef = 1.0f - min(sqrt(fireDiff2.r + fireDiff2.g + fireDiff2.b) * 2.0f, 1.0f); // 2 is here, because if only one component of the RGB is away half it's scale, we consider the color as cold
  fout.addTemp = fireCoef * 500.0f;

  // The colder the particle is, the more transparent it appears
  if (thermal) 
  {
    fout.color.a = lerp(fout.color.a * 0.2f, fout.color.a, fireCoef);
  }

  return fout;
}

/* Particles used for cover vehicles in Ti */
FOut PFSpriteExtTi(SSprite input, bool sbShadows, bool thermal)
{
  // If we use SM2 and thermal vision, use PFSpriteSimple instead, as we run out of available instructions in that case
#if _SM2
  if (thermal) return PFSpriteSimple(input, sbShadows, thermal);
#endif

  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    0);
  fout.light.indirect *= input.modulateColor;
  fout.light.direct *= input.modulateColor;

  // Calculate the z offset to perform the non-linear disappearing
  half4 tex0 = tex2D0(input.tDiffuseMap.xy);

  // Get the depth buffer value in meters
  half z = 1.0f / tex2D(sampler1, input.tPosition.xy+(PSC_InvW_InvH_X_X.xy*0.5)).r;

  // Calculate the alpha coefficient (soft particles)
  //half zOffset = input.tPosition.w * (1.0f - tex0.a);
  //half alphaCoef = saturate((z - (input.tPosition.z + zOffset)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w * (1.0f - tex0.a)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w + input.tPosition.w*tex0.a)) / input.tPosition.w);
  //half alphaCoef = saturate((z - input.tPosition.z - input.tPosition.w)) / input.tPosition.w + tex0.a);
  half alphaCoef = saturate((z - input.tPosition.z)/input.tPosition.w + tex0.a);

  // Calculate the color
  fout.color = GetColorDiffuse(tex0,input.ambientColor.a * input.modulateColor.a * alphaCoef, 0);

  fout.psBlend = true;
  if (fout.color.a<0)
  {
    fout.addBlend = true;
    fout.color.a = -fout.color.a;
  }

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = 1; // Sprite is a special object - it actually doesn't matter where the sun is shining from
  fout.addTemp = 1.0;

  // The colder the particle is, the more transparent it appears
  if (thermal) 
  {
    fout.color.a = lerp(fout.color.a * 0.2f, fout.color.a, 1.0);
  }

  return fout;
}

//////////////////////////////////////////////

// shader for volumetric cloud puff (simple version - when depth texture is not available)
FOut PFVolCloudSimple(SSprite input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light = GetLight(
    input.ambientColor,
    half3(0, 0, 0),
    0);
  fout.light.indirect *= input.modulateColor;
  fout.light.direct *= input.modulateColor;

  // Calculate the z offset to perform the non-linear disappearing
  half4 tex0 = tex2D0(input.tDiffuseMap.xy);

  // Calculate the color
  fout.color = GetColorDiffuse(tex0,input.ambientColor.a * input.modulateColor.a, 0);
  fout.psBlend = true;
  if (fout.color.a<0)
  {
    fout.addBlend = true;
    fout.color.a = -fout.color.a;
  }

  // make sure the "color" can be represented as half with no overflow
  fout.color = min(fout.color,MAX_HALF);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;

  // Remember the nDotL
  fout.nDotL = 1; // Sprite is a special object - it actually doesn't matter where the sun is shining from

  return fout;
}

// shader for volumetric cloud puff
FOut PFVolCloud(SSprite input, bool sbShadows, bool thermal)
{ 
  FOut fout = InitFOut();

  // init lights
  fout.light.indirect = PSC_AE.rgb;
  fout.light.direct = PSC_DForced * input.modulateColor;
  fout.light.specular = half3(0,0,0);
  fout.light.specularEnv = half3(0,0,0);
  fout.light.diffuseShadowCoef = 1.0f;

  // Calculate the z offset to perform the non-linear disappearing
  half4 tex0 = tex2D0(input.tDiffuseMap.xy);

  // Get the depth buffer value in meters
  half z = 1.0f / tex2D(sampler1, input.tPosition.xy+(PSC_InvW_InvH_X_X.xy*0.5)).r;

  // Calculate the alpha coefficient (soft particles)
  half alphaCoef = saturate((z - input.tPosition.z)/input.tPosition.w + tex0.a);

  // Calculate the color
  fout.color = GetColorDiffuse(tex0,input.ambientColor.a * input.modulateColor.a * alphaCoef, 0);

  fout.psBlend = true;
  if (fout.color.a<0)
  {
    fout.addBlend = true;
    fout.color.a = -fout.color.a;
  }

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.specularColor_landShadow.a;
  fout.landShadow = 1.0f;

  // Remember the nDotL
  fout.nDotL = 1; // Sprite is a special object - it actually doesn't matter where the sun is shining from

  fout.airCoolingFactor = 0.0;

  // The colder the particle is, the more transparent it appears
  if (thermal) 
  {
    // The closer is the base color to fire color, the higher the particle color is
    half3 fireColor = half3(1, 0.5, 0);
    half3 fireDiff = (fout.color.rgb - fireColor);
    half3 fireDiff2 = fireDiff * fireDiff;
    half fireCoef = 1.0f - min(sqrt(fireDiff2.r + fireDiff2.g + fireDiff2.b) * 2.0f, 1.0f); // 2 is here, because if only one component of the RGB is away half it's scale, we consider the color as cold
    fout.addTemp = fireCoef * 500.0f;

    fout.color.a = lerp(fout.color.a * 0.2f, fout.color.a, fireCoef);
  }

  return fout;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
FOut PFSuperExt(SSuper input, bool sbShadows, bool thermal)
{
  // Get the LWS position
  float3 lwsPosition = float3(input.stn2lws0.w, input.stn2lws1.w, input.stn2lws2.w);

  // Get the normal in LWS space
  half3 lwsNormal;
  {
    // Get the normal from texture in (STN space)
#if _SM2
    half3 normal = half3(0, 0, 1);
#else
    half4 normal4 = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
    half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
    half3 normal = /*normalize*/(dxt5FixedNormal * 2 - 1); // No normalization needed, it will be done after transformations
#endif

    // Transform normal to LWS
    lwsNormal.x = dot(input.stn2lws0.xyz, normal);
    lwsNormal.y = dot(input.stn2lws1.xyz, normal);
    lwsNormal.z = dot(input.stn2lws2.xyz, normal);

    // Normalize the result, because the stn2lws may be inaccurate due to per vertex artefacts
    lwsNormal = normalize(lwsNormal);
  }

  // Calculate vector to eye
  float3 lwsEye = normalize(-lwsPosition.xyz);

  // Calculate the halfway vector in LWS space
  float3 lwsHalfway = normalize(lwsEye + PSC_WLight);

  // Calculate the O vector (the reflection vector)
  float3 o = lwsNormal.xyz * dot(lwsNormal.xyz, lwsEye) * 2.0f - lwsEye;

  // Calculate the specular power and environmental map power coef
  // Inner Max is here to avoid zero division
  // Outer Max is here to not to cause aliasing in DDX and DDY (to not to make them smaller than 1)
  half specularPower = PSC_Specular.a * tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx).b;
  half specularPowerEnvCoef = max(500/max(specularPower, 1), 1); // Heuristic conversion of specularPower to env. map. bluring coefficient

  // Get the color from the environmental map
  float2 envT = o.xy * float2(1, -1) * 0.5 + 0.5;
#if _SM2
  half3 envColor = /*tex2D(sampler7, envT) **/ PSC_GlassEnvColor.rgb;
#else
  half3 envColor = tex2Dgrad(sampler7, envT, ddx(envT)*specularPowerEnvCoef, ddy(envT)*specularPowerEnvCoef) * PSC_GlassEnvColor.rgb;
#endif

  // Get the Fresnel reflection coefficient
#if _SM2
  half fresnelCoef = 0.5;
#else
  half fresnelCoef = tex2D(sampler6, float2(dot(lwsNormal, o), 0.5)).a;
#endif

  // Get the specular map content
  half4 specular = tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx);

  // Get the specular coefficient
  half specularCoef = fresnelCoef * specular.g;

  FOut fout = InitFOut();

  half4 ambientShadow = tex2D(sampler4, input.tShadowMap_SpecularMap.xy);

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightSuper(PointSpotLighting(input.tPSFog.yzw, input.landShadow.xyz) + input.ambientColor.rgb,
    lwsNormal, lwsHalfway, envColor, fresnelCoef, specular, ambientShadow, coefDiffuse);

  fout.light.indirect += tex2D(sampler8, input.tDiffuseMap_NormalMap.xy) * PSC_NightEmissiveCoef;

  fout.color = GetColorSuper(tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_MacroMap.xy),
    tex2D(sampler3, input.tDetailMap_MacroMap.wzyx),
    input.ambientColor.a, specularCoef);

  //  fout.color = float4(lwsNormal.rgb, 1);

  // Calculate the fog
  fout.fog = input.tPSFog.x;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  return fout;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
FOut PFSuper(SSuper input, bool sbShadows, bool thermal)
{
  // Get the LWS position
  float3 lwsPosition = float3(input.stn2lws0.w, input.stn2lws1.w, input.stn2lws2.w);

  // Get the normal in LWS space
  half3 lwsNormal;
  {
    // Get the normal from texture in (STN space)
#if _SM2
    half3 normal = half3(0, 0, 1);
#else
    half4 normal4 = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
    half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
    half3 normal = /*normalize*/(dxt5FixedNormal * 2 - 1); // No normalization needed, it will be done after transformations
#endif

    // Transform normal to LWS
    lwsNormal.x = dot(input.stn2lws0.xyz, normal);
    lwsNormal.y = dot(input.stn2lws1.xyz, normal);
    lwsNormal.z = dot(input.stn2lws2.xyz, normal);

    // Normalize the result, because the stn2lws may be inaccurate due to per vertex artefacts
    lwsNormal = normalize(lwsNormal);
  }

  // Calculate vector to eye
  float3 lwsEye = normalize(-lwsPosition.xyz);

  // Calculate the halfway vector in LWS space
  float3 lwsHalfway = normalize(lwsEye + PSC_WLight);

  // Calculate the O vector (the reflection vector)
  float3 o = lwsNormal.xyz * dot(lwsNormal.xyz, lwsEye) * 2.0f - lwsEye;

  // Calculate the specular power and environmental map power coef
  // Inner Max is here to avoid zero division
  // Outer Max is here to not to cause aliasing in DDX and DDY (to not to make them smaller than 1)
  half specularPower = PSC_Specular.a * tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx).b;
  half specularPowerEnvCoef = max(500/max(specularPower, 1), 1); // Heuristic conversion of specularPower to env. map. bluring coefficient

  // Get the color from the environmental map
  float2 envT = o.xy * float2(1, -1) * 0.5 + 0.5;
#if _SM2
  half3 envColor = /*tex2D(sampler7, envT) **/ PSC_GlassEnvColor.rgb;
#else
  half3 envColor = tex2Dgrad(sampler7, envT, ddx(envT)*specularPowerEnvCoef, ddy(envT)*specularPowerEnvCoef) * PSC_GlassEnvColor.rgb;
#endif

  // Get the Fresnel reflection coefficient
#if _SM2
  half fresnelCoef = 0.5;
#else
  half fresnelCoef = tex2D(sampler6, float2(dot(lwsNormal, o), 0.5)).a;
#endif

  // Get the specular map content
  half4 specular = tex2D(sampler5, input.tShadowMap_SpecularMap.wzyx);

  // Get the specular coefficient
  half specularCoef = fresnelCoef * specular.g;

  FOut fout = InitFOut();

  half4 ambientShadow = tex2D(sampler4, input.tShadowMap_SpecularMap.xy);

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightSuper(PointSpotLighting(input.tPSFog.yzw, input.landShadow.xyz) + input.ambientColor.rgb,
    lwsNormal, lwsHalfway, envColor, fresnelCoef, specular, ambientShadow, coefDiffuse);
  
  fout.color = GetColorSuper(tex2D0(input.tDiffuseMap_NormalMap.xy),
    tex2D(sampler2, input.tDetailMap_MacroMap.xy),
    tex2D(sampler3, input.tDetailMap_MacroMap.wzyx),
    input.ambientColor.a, specularCoef);
//  fout.color = float4(lwsNormal.rgb, 1);

  fout.htCoef = ambientShadow.g;

  // Calculate the fog
  fout.fog = input.tPSFog.x;

  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;

  // Remember the nDotL
  fout.nDotL = coefDiffuse;

  fout.tTIMap.xy = input.tDiffuseMap_NormalMap.xy;

  return fout;
}

FOut PFSuperAToC(SSuper input, bool sbShadows, bool thermal)
{
  return PFSuper(input, sbShadows, thermal);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Super shader modification for hair rendering 
We use normal super shader and then use alpha handling of tree adv shaders.
This way AtoC can be used. 
*/
FOut PFSuperHairBody(SSuper input, bool sbShadows, bool thermal, bool atoc)
{
  FOut res = PFSuper(input, sbShadows, thermal);

  res.color = GetColorDiffuseDiscreteNoiseAdv(res.color, 1.0, atoc);
  return res;
}


FOut PFSuperHair(SSuper input, bool sbShadows, bool thermal)
{
  return PFSuperHairBody(input, sbShadows, thermal, false);
}

FOut PFSuperHairAtoC(SSuper input, bool sbShadows, bool thermal)
{
  return PFSuperHairBody(input, sbShadows, thermal, true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
half4 PFSkin0(SSkin input, bool sbShadows, bool thermal)
{
  // AS
  half4 tex4 = tex2D( sampler4, input.tShadowMap_SpecularMap.xy ); // g...ambient shadow, b...diffuse shadow coef
  return tex4;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
SPFSkinOut PFSkin1( SSkin input, half4 mapAS, half shadowCoef, bool sbShadows, bool thermal )
{
  SPFSkinOut o = (SPFSkinOut)0;
  // Get the LWS position
  float3 lwsPosition = float3(input.stn2lws0.w, input.stn2lws1.w, input.stn2lws2.w);
  // vertex lights (used as ambient/indirect)
  half3 sumVertLight = input.ambientColor.rgb;
  // CO
  half4 tex0 = tex2D0( input.tDiffuseMap_NormalMap.xy ); // multiplied by PSC_MaxColor  ...purpose?
  // NO
  half4 tex1 = tex2D( sampler1, input.tDiffuseMap_NormalMap.wz );
  // MC // puvodne DT
  half4 tex2 = tex2D( sampler2, input.tDetailMap_MacroMap.wz ); // ?
  // SDM // puvodne MC
  half4 tex3 = tex2D( sampler3, input.tDiffuseMap_NormalMap.xy ); //input.tDetailMap_MacroMap.wz );
  // AS = input mapAS
  //half4 tex4 = mapAS; //tex2D( sampler4, input.tShadowMap_SpecularMap.xy ); // g...ambient shadow, b...diffuse shadow coef
  // SMDI
  half4 tex5 = tex2D( sampler5, input.tShadowMap_SpecularMap.wz );
  
  // Get the normal in LWS space (lwsNormal)
  half3 normW;
  {
    // Get the normal from texture in (STN space)
#if _SM2
    half3 normal = half3(0, 0, 1);
#else
    half4 normal4 = tex1;
    half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
    half3 normal = (dxt5FixedNormal * 2 - 1); // No normalization needed, it will be done after transformations
#endif
    // Transform normal to LWS
    normW.x = dot(input.stn2lws0.xyz, normal);
    normW.y = dot(input.stn2lws1.xyz, normal);
    normW.z = dot(input.stn2lws2.xyz, normal);
    // Normalize the result, because the stn2lws may be inaccurate due to per vertex artefacts
    normW = normalize( normW );
  }
  // Calculate vector to camera (lwsEye)
  float3 dirToCamW = normalize(-lwsPosition.xyz);
  // Calculate the halfway vector in LWS space   (lwsHalfway)
  float3 halfVecW = normalize( dirToCamW.xyz + PSC_WLight.xyz );
  // Calculate the reflection vector (o)
  //float3 reflVecW = normW.xyz * dot( normW.xyz, dirToCamW.xyz ) * 2.0f - dirToCamW.xyz;
  
  half nDotL = dot( normW.xyz, PSC_WLight.xyz );  // PSC_WLight ..Main light direction in world coordinates
  // nDotL modified by shadow
  half nDotLSh = min( nDotL, shadowCoef * 1.3 - 0.3 ); // little darker shadow
  //half nDotLSh = min( nDotL, -0.3 );
  
  half coefDiffuseBack = max( -nDotL, 0.0h ); // inverse direction light coef
  half dotCN = dot( -dirToCamW, normW );
  half3 reflVecW = -dirToCamW - 2.0h * dotCN * normW;
  
  // Calculate the specular power and environmental map power coef
  // Inner Max is here to avoid zero division
  // Outer Max is here to not to cause aliasing in DDX and DDY (to not to make them smaller than 1)
  half specularPower = PSC_Specular.a * tex5.b;
  half specularPowerEnvCoef = max(500/max(specularPower, 1), 1); // Heuristic conversion of specularPower to env. map. bluring coefficient
  
  // Get the color from the environmental map
  float2 envT = reflVecW.xy * float2(1, -1) * 0.5 + 0.5;
#if _SM2
  half3 envColor = PSC_GlassEnvColor.rgb;
#else
  half3 envColor = tex2Dgrad(sampler7, envT, ddx(envT)*specularPowerEnvCoef, ddy(envT)*specularPowerEnvCoef) * PSC_GlassEnvColor.rgb;
#endif
  
  // Get the Fresnel reflection coefficient
#if _SM2
  half fresnelCoef = 0.5;
#else
  half fresnelCoef = tex2D( sampler6, float2( dot( normW.xyz, reflVecW.xyz ), 0.5 ) ).a;
#endif
  
// params from constant regs 
  half3 matUnscatteredColor = PSC_SkinEffectPars[0].rgb;
  half hemisphericSpecLevel = PSC_SkinEffectPars[1].a;
  half unscatteredWeight = PSC_SkinEffectPars[1].r;
  half epidermalWeight = PSC_SkinEffectPars[1].g;
  half subdermalWeight = PSC_SkinEffectPars[1].b;
  half epidermalMul = PSC_SkinEffectPars[2].r;
  half epidermalAdd = PSC_SkinEffectPars[2].g;
  half epidermalDarkRimMul = PSC_SkinEffectPars[2].b;
  half epidermalDarkRimAdd = PSC_SkinEffectPars[2].a;
  half subdermalMul = PSC_SkinEffectPars[3].r; // 0.35
  half subdermalAdd = PSC_SkinEffectPars[3].g; // 0.65
  half subdermalDarkRimMul = PSC_SkinEffectPars[3].b;
  half subdermalDarkRimAdd = PSC_SkinEffectPars[3].a;

// default params
  /*half3 matUnscatteredColor = half3( 0.6, 0.8, 1.0 );
  half hemisphericSpecLevel = 1.0;
  half unscatteredWeight = 0.20; // 0.10
  half epidermalWeight = 0.4; // 0.4
  half subdermalWeight = 0.4; // 0.5
  half epidermalMul = 0.8;
  half epidermalAdd = 0.2;
  half epidermalDarkRimMul = -0.3;
  half epidermalDarkRimAdd = 0.7;
  half subdermalMul = 0.65;
  half subdermalAdd = 0.35;
  half subdermalDarkRimMul = -0.7;
  half subdermalDarkRimAdd = 0.3;*/
  
  // ...from textures
  half skinMask = tex3.a;
  half3 blendCoMc = lerp( tex0.rgb, tex2.rgb, tex2.a );
  half3 unscatteredColor = blendCoMc.rgb * matUnscatteredColor.rgb;
  half3 epidermalColor = blendCoMc.rgb;
  half3 subdermalColor = lerp( tex3.rgb, tex2.rgb, tex2.a );

  // Light from the environment - roughly Ambient, DForced and Emmisive
  half3 indirectLight;
  // Light from the main light source (light that disappears in a shadow) - roughly the diffuse light
  half3 directLight;
  // specular of main light
  half3 specularLight;
  // hemispheric specular
  half3 specularLightHemi;
  // Specular light reflected from the environment (shadows doesn't affect it)
  half3 specularEnvLight;
  // Diffuse shadow value, used where no ordinary shadows are available (f.i. for objects in distance)
  half diffuseShadowCoefLight = mapAS.b; // AS
  
  // Get the specular coefficient
  half specularCoef = fresnelCoef * tex5.g; // SMDI.g ...specular level
  
// INDIRECT ( main light + bidir + hemi)
  indirectLight = ( GetIndirectLightLerp( normW.y * 0.5 + 0.5 ) // hemispheric light
    + PSC_LDirectionGround_DiffuseBack.rgb * coefDiffuseBack ) * mapAS.g; //+ input.ambientColor.rgb;

// DIRECT ( main light ...scattered and unscattered )
  // dark rim ("softening" factors)
  half camFactorEp = epidermalDarkRimMul * dotCN + epidermalDarkRimAdd;
  half camFactorSu = subdermalDarkRimMul * dotCN + subdermalDarkRimAdd;

  half3 unscatteredColorFinal = lerp( epidermalColor.rgb, unscatteredColor.rgb * unscatteredWeight, skinMask );
  
  // CO skin for hemispheric and bidir
  half3 colorSum = epidermalColor.rgb * ( matUnscatteredColor.rgb * unscatteredWeight + epidermalWeight )
    + subdermalColor.rgb * subdermalWeight;
  half3 colorFinal = lerp( epidermalColor.rgb, colorSum.rgb, skinMask );
  
  // CO skin for per vertex lights
  //half3 weights2 = half3( 0.2, 0.4, 0.4 );
  //half3 colorSum2 = epidermalColor.rgb * ( matUnscatteredColor.rgb * weights2.x + weights2.y )
  //  + subdermalColor.rgb * weights2.z;
  //half3 colorFinal2 = lerp( epidermalColor.rgb, colorSum2.rgb, skinMask );
  
  half3 directUnL1 = saturate( nDotLSh ) * unscatteredColorFinal;
  
  //half3 directUnL1 = saturate( nDotL ) * unscatteredColor.rgb * unscatteredWeight;
  half3 directEpL1 = saturate( nDotLSh * epidermalMul + epidermalAdd ) * epidermalColor.rgb * camFactorEp * epidermalWeight * skinMask;
  half3 directSuL1 = saturate( nDotLSh * subdermalMul + subdermalAdd ) * subdermalColor.rgb * camFactorSu * subdermalWeight * skinMask;
  // compose direct light layers
  directLight = PSC_Diffuse.rgb * ( directUnL1 + directEpL1 + directSuL1 );
    
// SPECULAR ( main light + hemi )
  specularLight = PSC_Specular.rgb * specularCoef * saturate( pow( saturate( dot( normW.xyz, halfVecW.xyz ) ), ( PSC_Specular.a * tex5.b ) ) )
    * saturate( nDotLSh * 5.0 + 0.25 ); // soft specular terminator
  // hemispheric specular
  specularLightHemi = GetIndirectLightLerp( reflVecW.y * 0.5 + 0.5 ) * specularCoef * hemisphericSpecLevel * mapAS.g;
  //specularEnvLight = envColor.rgb * PSC_GlassMatSpecular.rgb * 2.0f * specularCoef; // 2 is here because specular color is halved
  specularEnvLight = 0; // currently not used
  
  //o.indirectSum = sumVertLight.rgb + indirectLight.rgb;
  o.indirectLight = indirectLight.rgb;
  o.vertexLight = sumVertLight.rgb;
  o.colorFinal = half4( colorFinal.rgb, input.ambientColor.a * tex0.a ); // rgb ...composed material color, a ...final alpha (input.ambientColor.a * tex0.a)
  //o.colorFinal2 = colorFinal2.rgb;
  o.directLight = directLight;
  o.specularLight = specularLight;
  o.specularIndirect = specularLightHemi.rgb + specularEnvLight.rgb; // specularLightHemi.rgb + specularEnvLight
  o.fogCoef = input.tPSFog;
  o.diffuseShadowCoef = diffuseShadowCoefLight; // diffuse shadow
  return o;
};


FOut PFSkin1Ti( SSkin input, half4 mapAS )
{ 
  // CO
  // half4 tex0 = tex2D0( input.tDiffuseMap_NormalMap.xy ); // multiplied by PSC_MaxColor  ...purpose?
  // NO
  half4 tex1 = tex2D( sampler1, input.tDiffuseMap_NormalMap.wz );

  // Get the normal in LWS space (lwsNormal)
  half3 normW;
  {
    // Get the normal from texture in (STN space)
#if _SM2
    half3 normal = half3(0, 0, 1);
#else
    half4 normal4 = tex1;
    half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
    half3 normal = (dxt5FixedNormal * 2 - 1); // No normalization needed, it will be done after transformations
#endif
    // Transform normal to LWS
    normW.x = dot(input.stn2lws0.xyz, normal);
    normW.y = dot(input.stn2lws1.xyz, normal);
    normW.z = dot(input.stn2lws2.xyz, normal);
    // Normalize the result, because the stn2lws may be inaccurate due to per vertex artefacts
    normW = normalize( normW );
  }
  
  FOut fout = InitFOut();
  // PSC_WLight ..Main light direction in world coordinates
  fout.nDotL = max(0, dot( normW.xyz, PSC_WLight.xyz ));
  fout.color = 1;//max(mapAS.g, 0.4);
  // Calculate the fog
  fout.fog = input.tPSFog;
  // Calculate the land shadow
  fout.landShadow = input.landShadow.a;
  fout.tTIMap = input.tDiffuseMap_NormalMap.xy;
  fout.light = (Light) 0;
  return fout;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// compute final output color using PFSkin1() fragment output and shadowCoef
float4 PFSkinFinColor( SPFSkinOut i, half shadowCoef )
{
  half4 final = 1;
  //half3 colorBlend = ( ( i.indirectSum + PSC_DForced.rgb * shadowCoef ) * i.colorFinal.rgb + i.directLight * shadowCoef );
  //half3 colorAdd = i.specularLight.rgb * shadowCoef + i.specularIndirect.rgb;
  half3 colorBlend = ( i.indirectLight.rgb + i.vertexLight.rgb ) * i.colorFinal.rgb + i.directLight;// + PSC_DForced.rgb * i.colorFinal.rgb;
  half3 colorAdd = i.specularLight.rgb + i.specularIndirect.rgb;
  final.rgb = colorBlend.rgb + colorAdd.rgb;
  final.a = i.colorFinal.a;
  final.rgb = ToRTColorSpace( ApplyFog( final.rgb, i.fogCoef ) );
  //final.rgb = pow( PSC_SkinEffectPars[3].rgb, 2.2 ); // test
  return final;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// function to retrieve shadow coef 
half GetShadowBasicSSSM( float4 tShadowBuffer, float2 vPos, half4 landShadow, half diffShadowCoef )
{
  half shadowCoef;
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half disappear = saturate( (tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
#ifdef _XBOX
  shadowCoef = disappear + tex2Dlod(samplerShadowMap, float4((vPos.xy + PSCGPU_TOffX_TOffY_X_X[0].xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - disappear);
#else
  shadowCoef = disappear + tex2Dlod(samplerShadowMap, float4((vPos.xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - disappear);
#endif
  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - diffShadowCoef) * disappear;  // fout.light.diffuseShadowCoef
  // Involve land shadow to shadow coefficient
  shadowCoef *= landShadow.a;
  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);
  return shadowCoef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//! function to retrieve shadow coef
half GetShadowSRnVidia( float4 tShadowBuffer, float2 vPos, half4 landShadow, half diffShadowCoef )
{
  float4 nVidiaSTRQ = float4(tShadowBuffer.xyz, 1);
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half disappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
  half shadowCoef = disappear + tex2Dlod(samplerShadowMap, nVidiaSTRQ).r * (1 - disappear);
  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - diffShadowCoef) * disappear;
  // Involve land shadow to shadow coefficient
  shadowCoef *= landShadow.a;
  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);
  return shadowCoef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//! function to retrieve shadow coef
half GetShadowSRDefault( float4 tShadowBuffer, float2 vPos, half4 landShadow, half diffShadowCoef )
{
  // Precomputed constants related to texture size
  const float texsize = PSC_SBTSize_invSBTSize_X_AlphaMult.x;
  const float invTexsize = PSC_SBTSize_invSBTSize_X_AlphaMult.y;
  // Calculate the fractional part of the texel addressing
  float2 dxy = frac(tShadowBuffer.xy * texsize);
  // Calculate weights for bilinear interpolation
  float3 pnz = float3(+1,-1,0);
  float4 fxy = dxy.xyxy * pnz.xxyy + pnz.zzxx;
  float4 weights = fxy.zzxx*fxy.wywy;
  // Get 4 shadow depths
  float4 shadowDepth = float4(
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(0,           invTexsize,0,0)).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(invTexsize,  0,0,0)).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + float4(invTexsize,  invTexsize,0,0)).r
    );
  // Get 4 shadow/not shadow values
  float4 sc = shadowDepth>=tShadowBuffer.z;
  // Use weights to get the shadow coefficient
  half shadowCoef = dot(sc, weights);
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half disappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
  shadowCoef = disappear + shadowCoef * (1 - disappear);
  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - diffShadowCoef) * disappear;
  // Involve land shadow to shadow coefficient
  shadowCoef *= landShadow.a;
  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);
  return shadowCoef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
FOut PFMulti(SMulti input, bool sbShadows, bool thermal)
{
  FOut fout = InitFOut();

  // Get the mask content
  half3 mask = tex2D(sampler4, input.tMSK_DT0.xy);

//   if (mask.b > 0.5)
//   {
//     mask = float3(0, 0, 1);
//   }
//   else
//   {
//     if (mask.g > 0.5)
//     {
//       mask = float3(0, 1, 0);
//     }
//     else
//     {
//       if (mask.r > 0.5)
//       {
//         mask = float3(1, 0, 0);
//       }
//       else
//       {
//         mask = float3(0, 0, 0);
//       }
//     }
//   }

  // Remember the texture coordinates for each stage (CO and NO textures) - it is a float4 structure, because we use it to select the last mipmap (20) as well
  float4 co0 = float4(input.tCO0_CO1.xy, 0, 20);
  float4 co1 = float4(input.tCO0_CO1.wz, 0, 20);
  float4 co2 = float4(input.tCO2_CO3.xy, 0, 20);
  float4 co3 = float4(input.tCO2_CO3.wz, 0, 20);

  // Remember the texture coordinates for each stage (DT (SMDI) textures)
  float2 dt0 = input.tMSK_DT0.wz;
  float2 dt1 = input.tDT1_DT2.xy;
  float2 dt2 = input.tDT1_DT2.wz;
  float2 dt3 = co3; // We don't have enough room for the last DT coordinate, so we use the same coordinates as in corresponding CO

  // Get the masked CO map
#if _SM2
  half3 mCO = half3(1, 1, 1);
#else
  half3 mCO = tex2D(sampler0, co0.xy).rgb*PSC_MaxColor;
  mCO = lerp(mCO, tex2D(sampler1, co1.xy).rgb, mask.r);
  mCO = lerp(mCO, tex2D(sampler2, co2.xy).rgb, mask.g);
  mCO = lerp(mCO, tex2D(sampler3, co3.xy).rgb, mask.b);
#endif

  // Get the masked avg CO map
#if _SM2
  half3 mCOavg = tex2Dbias(sampler0, co0).rgb*PSC_MaxColor;
  mCOavg = lerp(mCOavg, tex2Dbias(sampler1, co1).rgb, mask.r);
  mCOavg = lerp(mCOavg, tex2Dbias(sampler2, co2).rgb, mask.g);
  //mCOavg = lerp(mCOavg, tex2Dbias(sampler3, co3).rgb, mask.b);
#else
  half3 mCOavg = tex2Dlod(sampler0, co0).rgb*PSC_MaxColor;
  mCOavg = lerp(mCOavg, tex2Dlod(sampler1, co1).rgb, mask.r);
  mCOavg = lerp(mCOavg, tex2Dlod(sampler2, co2).rgb, mask.g);
  mCOavg = lerp(mCOavg, tex2Dlod(sampler3, co3).rgb, mask.b);
#endif

  // Get the masked NO map
#if _SM2
  half4 mNO = half4(0.5, 0.5, 1, 1);
#else
  half4 mNO = tex2D(sampler11, co0);
  mNO = lerp(mNO, tex2D(sampler12, co1), mask.r);
  mNO = lerp(mNO, tex2D(sampler13, co2), mask.g);
  mNO = lerp(mNO, tex2D(sampler14, co3), mask.b);
#endif

  // Get the masked SMDI map
  half4 mSMDI;
#if _SM2
  if (thermal)
  {
    mSMDI = half4(0, 0, 0, 1);
  }
  else
#endif
  {
    mSMDI = tex2D(sampler5, dt0);
    mSMDI = lerp(mSMDI, tex2D(sampler6, dt1), mask.r);
    mSMDI = lerp(mSMDI, tex2D(sampler7, dt2), mask.g);
    mSMDI = lerp(mSMDI, tex2D(sampler8, dt3), mask.b);
  }

  // Sample the macro map
  half4 macro = tex2D(sampler9, input.tMSK_DT0.xy);

  // Modify the base color by macro map (the same turn is used in Terrain shader)
  // See COLOR_MODIFICATION
  half3 adjust = (macro.rgb/mCOavg);
  adjust = min(2,max(adjust,0));
  mCO = lerp(mCO, mCO * adjust, macro.a);

  half4 ambientShadow = tex2D(sampler10, input.tMSK_DT0.xy);

  float3 skinnedNormal;
  skinnedNormal.xy = float2(input.tLightLocal.w, input.tUpLocal.w);
  skinnedNormal.z = sign(input.halfway_landShadow.w) * sqrt(1.0 - dot(skinnedNormal.xy, skinnedNormal.xy));

  // Calculate the lights
  half coefDiffuse;
  fout.light = GetLightNormalSpecularDIAS(PointSpotLighting(input.tPSFog.yzw, skinnedNormal) + input.ambientColor,
    mNO, input.tLightLocal.xyz, input.halfway_landShadow.xyz, input.tUpLocal.xyz,
    mSMDI, ambientShadow, coefDiffuse);

  // Calculate the color
  fout.color = GetColorDiffuseDetail(half4(mCO * (thermal ? 0.5 : 1.0), 1), mSMDI.rrr, input.ambientColor.a, 0);

  // Calculate the fog
  fout.fog = input.tPSFog;

  // Calculate the land shadow
  fout.landShadow = input.ambientColor.a;

  fout.addTemp = ambientShadow.g * 0.15;

  fout.tTIMap.xy = input.tCO0_CO1.xy;

  // Remember the nDotL
  fout.nDotL = coefDiffuse.x;

  return fout;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FOut PFThermalTreeBody(STree input, bool sbShadows, float4 tShadowBuffer, float2 vPos, bool simple, bool aToC)
{
  FOut fout = InitFOut();

  half4 primTex = tex2D0(input.tDiffuseMap_NormalMap.xy);
  half4 normalMap = tex2D(sampler1, input.tDiffuseMap_NormalMap.wzyx);
  half4 macroMap = tex2D(sampler2, input.tShadowMap.xy);
  half ambientAtten = macroMap.a;
  half diffuseAtten = lerp(1, macroMap.a, PSC_Diffuse.a);

  if (simple) ambientAtten = 1, diffuseAtten = 1;
  FORCEBRANCH if (!EarlyOut || primTex.a>0 && !RejectAll)
  {
    // Calculate the lights and diffuse attenuation
    half nDotL;
    {
      half3 normal = normalMap.rgb * 2 - 1;

      // perform ambient attenuation

      fout.light.indirect = ambientAtten * input.ambientColor.rgb + GetIndirectLight(dot(normal, input.tUpLocal.rgb));

      // perform diffuse attenuation via rescaling diffuse result
      half frontBack = dot(normal, input.lightLocal_landShadow.rgb);
      half frontCoef = max(frontBack, 0);
      half backCoef = max(-frontBack, 0);
      half lightCoef = backCoef * PSC_LDirectionGround_DiffuseBack.a + frontCoef;
      fout.light.direct = diffuseAtten * lightCoef * PSC_Diffuse.rgb + PSC_DForced;
      

      // Calculate specular light
      half4 litVector = lit(frontBack, dot(normal, input.tHalfway.xyz), PSC_Specular.a);
      nDotL = litVector.y;
      half coefSpecular = litVector.z;
      coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)
      fout.light.specular = PSC_Specular * coefSpecular * diffuseAtten;
      fout.light.specularEnv = 0;

      if (simple) fout.light.specular = 0;

      fout.light.diffuseShadowCoef = 1;
    }

    // Calculate the color
    fout.color = GetColorDiffuseDiscreteNoise( primTex * half4(macroMap.rgb, 1), input.ambientColor.a, normalMap.a, aToC );
    fout.addTemp = fout.light.direct + fout.light.indirect;

    // Calculate the fog
    fout.fog = input.tPSFog;

    // Remember the nDotL
    fout.nDotL = nDotL;

    fout.psBlend = false; // never using blending, avoid modulating color by alpha

    // Calculate the land shadow
    fout.landShadow = input.lightLocal_landShadow.a;
  }
  else
  {
    fout.color = GetFinalColorEmpty();
  }

  return fout;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Special pixel shaders - no shadow versions required

/// used for sky background
half4 PSInterpolation(SBasic input) : COLOR
{
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  // reconstruct colors from DXT5 compression
  // do it in such a way that even DXT1 textures work
  half3 diffuseFinal = half3(0, 1 - diffuse.a, 0) + diffuse;

  return half4(ToRTColorSpace(input.ambient.rgb * diffuseFinal), 1);
}

/// interpolation with alpha (used for sky clouds)
half4 PSInterpolationAlpha(SBasic input) : COLOR
{
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  return half4(ToRTColorSpace((input.ambient.rgb * diffuse.rgb) * diffuse.a), diffuse.a);
}

//////////////////////////////////////////////

/// used for sky horizon layer

float4 PSHorizon(SBasic input) : COLOR
{
  // similar to PSInterpolation, but there is no DXT5 texture format trick
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  // reconstruct colors from DXT5 _sky compression
  diffuse.ag = 1-diffuse.ga;
  //half3 diffuseFinal = half3(0, 1 - diffuse.a, 0) + diffuse;
  //return half4(0,0,0,diffuse.a);
  
  return ApplySrcAlpha(half4(ToRTColorSpace(input.ambient.rgb*diffuse.rgb), diffuse.a));
}

// shader for underwater occlusion object
half4 PSUnderwaterOcclusion(SBasic input): COLOR
{
  float4 fogInfo = input.color0;
  // coloring of the bottom part of object based on underwater fog 
  // fogInfo.x contains fog coef, fogInfo.y contains height above the fog blending line (see VFUnderwaterOcclusion)
  float waterFogCoef = fogInfo.x * saturate((1.0f - fogInfo.y)/1.0f); 

  return half4(ToRTColorSpace(PSC_WaterFogColor.rgb * waterFogCoef), waterFogCoef);
}

half4 PSUnderwaterOcclusionThermal(SBasic input): COLOR
{
  // LUKE TODO 

  float4 fogInfo = input.color0;

  // coloring of the bottom part of object based on underwater fog 
  // fogInfo.x contains fog coef, fogInfo.y contains height above the fog blending line (see VFUnderwaterOcclusion)
  float waterFogCoef = fogInfo.x * saturate((1.0f - fogInfo.y)/1.0f); 

  return half4(ToRTColorSpace(PSC_FogColor.rgb * waterFogCoef), waterFogCoef);
}


half4 PSThermalInterpolation(SBasic input) : COLOR
{
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  // reconstruct colors from DXT5 _sky compression
  diffuse.ag = 1 - diffuse.ga;

  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  float offset = 0.5f / 16.0f;
  half airTemperature = tex2D(samplerTc, float2(0 + offset, 2.0f/16.0f + offset)).r;

   // The closer is the base color to the clear sky color, the lower the temperature is (the minimum in real is 0K, however we use some higher value here (TempMin),
  // because the scale is controlled linearily here and using 0K we would see black almost all the time)
  // The far is the base color to the clear sky color (the more cloudy it is), the more is the temperature similar to the air temperature
  float simCoef = GetColorSimilarityCoef(half3(0, 0.27734375, 0.6328125), diffuse, 1.4);

  float temperature = lerp(airTemperature - 0.1, airTemperature + 0.9, simCoef);

  // Encode the temperature into RGB
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);
  return half4(ToRTColorSpace(rgbEncodedTemperature), 1);
}

half4 PSThermalInterpolationAlpha(SBasic input) : COLOR
{
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  float offset = 0.5f / 16.0f;
  half airTemperature = tex2D(samplerTc, float2(0 + offset, 2.0f/16.0f + offset)).r;

  // The closer is the base color to the clear sky color, the lower the temperature is (the minimum in real is 0K, however we use some higher value here (TempMin),
  // because the scale is controlled linearily here and using 0K we would see black almost all the time)
  // The far is the base color to the clear sky color (the more cloudy it is), the more is the temperature similar to the air temperature
  float simCoef = GetColorSimilarityCoef(half3(0, 0.27734375, 0.6328125), diffuse, 1.4);

  float temperature = lerp(airTemperature - 0.1, airTemperature + 0.9, simCoef);

  // Encode the temperature into RGB
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);
  return half4(ToRTColorSpace(rgbEncodedTemperature * diffuse.a), diffuse.a);
}

float4 PSHorizonThermal(SBasic input) : COLOR
{
  // similar to PSInterpolation, but there is no DXT5 texture format trick
  half4 diffuseMapA = tex2D(sampler0, input.diffuseMap.xy);
  half4 diffuseMapB = tex2D(sampler1, input.diffuseMap.xy);

  // Interpolate between A and B
  half4 diffuse = lerp(diffuseMapB, diffuseMapA, input.ambient.a);

  // reconstruct colors from DXT5 _sky compression
  diffuse.ag = 1-diffuse.ga;

  // Get temperature of the air (TC table, WhiteUnlit place, first place in the table)
  float offset = 0.5f / 16.0f;
  half airTemperature = tex2D(samplerTc, float2(0 + offset, 2.0f/16.0f + offset)).r;

  float temperature = lerp(airTemperature - 0.1, airTemperature + 0.9, dot(diffuse.rgb, half3(0.299, 0.587, 0.114)));

  // Encode the temperature into RGB
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(temperature, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);
  return half4(ToRTColorSpace(rgbEncodedTemperature) * diffuse.a, diffuse.a);
}

//////////////////////////////////////////////

float4 PSCloud(SBasic input) : COLOR
{
  return ApplySrcAlpha(ToRTColorSpace(tex2D0(input.diffuseMap.xy) * (half4(PSC_AE.rgb + PSC_DForced.rgb, 0) + input.ambient)));
}

float4 PSCloudThermal(SBasic input) : COLOR
{
  return ApplySrcAlpha(ToRTColorSpace(tex2D0(input.diffuseMap.xy) * 0.5));
}

//////////////////////////////////////////////

float4 PSWhite() : COLOR
{
  return float4(1, 1, 1, 1);
}

//////////////////////////////////////////////

/// simple pixel shader intended for UI and other 2D stuff
/*
float4 PSNonTL(float4 tDiffuseMap : TEXCOORD0, float4 ambientColor : TEXCOORD6) : COLOR
{
  return tex2DLOD0(tDiffuseMap.xy) * ambientColor;
}
*/

/*
float4 PSNonTL(SNormal input) : COLOR
{
  return ApplySrcAlpha(tex2DLOD0(input.tDiffuseMap.xy) * input.ambientColor);
}
*/


// NonTL with outline effect (for text)
float4 PSNonTL(SNonTL input) : COLOR
{
  // some params:
  half4 final;
  half4 ac = input.ambientColor;
  float2 tc = input.diffuseMap.xy;
  half4 c0 = tex2Dlod( sampler0, float4(tc.xy, 0, 0) );

  if ( PSC_UIPars.x > 0 ) // use outlining if alpha in ambient color is negative
  {
    half3 outlineColor = half3(0, 0, 0);
    float2 invTextureSizes = float2( (PSC_UIPars.x + 0.5) * PSC_UIPars.y, (PSC_UIPars.x + 0.5) * PSC_UIPars.z );

    half a; // outlining alpha
    a = tex2Dlod ( sampler0, float4(tc.x + invTextureSizes.x, tc.y + invTextureSizes.y, 0, 0) ).a;
    a += tex2Dlod( sampler0, float4(tc.x - invTextureSizes.x, tc.y + invTextureSizes.y, 0, 0) ).a;
    a += tex2Dlod( sampler0, float4(tc.x + invTextureSizes.x, tc.y - invTextureSizes.y, 0, 0) ).a;
    a += tex2Dlod( sampler0, float4(tc.x - invTextureSizes.x, tc.y - invTextureSizes.y, 0, 0) ).a;
    a = saturate( a * PSC_UIPars.w );
    final.a = max( c0.a, a );
    final.rgb = c0.rgb * c0.a;
    final.rgb = lerp( outlineColor, c0.rgb, c0.a );
  }
  else
  {
    final = c0;
  }

  half4 outColor = ApplySrcAlpha(final * ac);
  DoAlphaTest(outColor.a);
  return outColor;
}

//////////////////////////////////////////////

float4 PSWhiteAlpha(float4 tDiffuseMap:TEXCOORD0) : COLOR
{
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  return ApplySrcAlpha(half4(1, 1, 1, diffuseMap.a));
}

//////////////////////////////////////////////

half4 PSReflect(half4 color : TEXCOORD_DIFFUSE_SI, float4 tDiffuseMap : TEXCOORD0) : COLOR
{
  // TEXCOORD_DIFFUSE_SI.a should contain shadow-encoded amount of diffuse lighting
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  return ApplySrcAlpha(ToRTColorSpace(half4(diffuseMap.rgb, color.a)));
}

//////////////////////////////////////////////

half4 PSReflectNoShadow(float4 tDiffuseMap : TEXCOORD0) : COLOR
{
  half4 diffuseMap = tex2D(sampler0, tDiffuseMap.xy);
  return half4(diffuseMap.rgb, 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// SEA WATER
////////////////////////////////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------------------
half WaterFresnel( half3 norm, half3 dir, half4 frp )
{
  // float const R0 = pow(1.0-refractionIndexRatio, 2.0) / pow(1.0+refractionIndexRatio, 2.0);
  // water R0 = 0.0203
  //fresnel = R0 + (  1 - R0 ) * pow( 1 - dot( dir, norm ), 5 );
  //float4 frp = FresnelPars0;
  return saturate( frp.x + ( 1 - frp.x) * pow( frp.y - frp.z * dot( dir, norm ), frp.w ) );
  //return saturate( frp.x + ( 1 - frp.x) * pow( saturate( 1 - dot( dir, norm ) ), frp.w ) ); // simlified
}




//SEM


//modificator of specular reflection, is different from normal to highlight th effect
#define SpecNormal              0.6


#ifdef WATER_REAL_REFRACTION
  #define WaterExtFactor         5.0
#else 
  #define WaterExtFactor         -10.0
#endif

//visibility in water, lower number = lower visibility
#define WATER_DEEPNESS_EXP      -1.2
// = exp(WaterExtFactor)

#define FoamNormalVertexZ       0.9
#define WaterFoamVisibility     1.0


#define LargeNormalBlend        PSC_CalmWaterPars1[0]
#define SmallNormalBlend        PSC_CalmWaterPars1[1]

#define ScreenScale             PSC_CalmWaterPars1[2].xy
#define ScreenOffset            PSC_CalmWaterPars1[2].zw

#define WeatherNormalTex        PSC_CalmWaterPars1[3].y
#define WeatherNormalVert       PSC_CalmWaterPars1[3].x
#define WeatherSpecular         PSC_CalmWaterPars1[3].z
#define WeatherSpecularPower    PSC_CalmWaterPars1[3].w

#define Weather10               PSC_CalmWaterPars1[4].x
#define LDotNScale              PSC_CalmWaterPars1[4].y
#define LDotNOffset             PSC_CalmWaterPars1[4].z
#define LocalTime               PSC_CalmWaterPars1[4].w

// inverse of projection matrix x-value (1.0f/projmatrix[0,0])
#define ProjMatrixInvX          PSC_CalmWaterPars1[5].x
// inverse of projection matrix y-value (1.0f/projmatrix[1,1])
#define ProjMatrixInvY          PSC_CalmWaterPars1[5].y
// 1.0f = we are underwater, 0.0f - we are above water
#define IsUnderWaterCoef        PSC_CalmWaterPars1[5].z

// coefficient used to multiply move value of refractions
#define RefractionMoveCoef      PSC_CalmWaterPars1[6].x
// minimal water surface opacity, when we are looking from above the water surface down
#define MinWaterOpacity         PSC_CalmWaterPars1[6].y
// coef used to multiply square root of distance of pixel from water surface when coputing final opacity of water surface
// waterOpacity = MinWaterOpacity + sqrt(distance)*WaterOpacityDistCoef;
#define WaterOpacityDistCoef    PSC_CalmWaterPars1[6].z
// opacity of water surface when we are under water
#define UnderwaterOpacity       PSC_CalmWaterPars1[6].w

// distance from object, where we start to fade the water surface to full opacity
#define WaterOpacityFadeStart   PSC_CalmWaterPars1[7].x
// length of water surface opacity fading
#define WaterOpacityFadeLength  PSC_CalmWaterPars1[7].y


//------------------------------------------------------------------------------------
/*
convert normal to usable format
*/
//------------------------------------------------------------------------------------
half3 ConvertNormal(half4 normal)
{
  half3 res;

  //not neeed z coord
  res.xy  = half2(normal.x + (1 - normal.a), normal.y) - 0.5;
  res.z   = 0.0f;//sqrt(1 - dot(res.xy, res.xy));  
    
  return res;
}


half3 ConvertNormal2(half4 normal)
{
  half3 res;

  //not neeed z coord
  res.xy  = normal.ga*2 - 1;
  res.z   = 0.0f;
    
  return res;
}



//------------------------------------------------------------------------------------
/*
modify local normal by global vertex normal
*/
//------------------------------------------------------------------------------------
half3 ModifyNormal(half3 texnormal, float3 vertexnormal)
{
  half3 normal ;//= normalize(texnormal + vertexnormal);
  
  float3 binormal   = normalize(float3(1,0,0) - vertexnormal*vertexnormal.x*WeatherNormalVert);
  float3 tangent    = cross(vertexnormal, binormal);
 
  normal.x = dot (binormal, texnormal);
  normal.y = dot (tangent, texnormal);
  normal.z = dot (vertexnormal, texnormal);  

  return normal;
}



//------------------------------------------------------------------------------------
/*
compute local normal by texture sampling and combining
*/
//------------------------------------------------------------------------------------
half3 ComputeNormal(float2 ntex1, float2 ntex2, float2 ntex3, half large, half small, out half3 largenormal)
{
  half4 normal0a = tex2D(sampler1,ntex1);
  half4 normal0b = tex2D(sampler1,ntex2);

  half3 n0a = ConvertNormal2(normal0a);
  half3 n0b = ConvertNormal2(normal0b);
 
  half4 normal1 = normal0b;
  half4 normal2 = tex2D(sampler1,ntex2 + 0.25);
  half4 normal3 = tex2D(sampler1,ntex2 + 0.5);
  half4 normal4 = tex2D(sampler1,ntex2 + 0.75);

  half4 normal5 = tex2D(sampler3,(ntex3));
  half4 normal6 = tex2D(sampler3,(ntex3 + 0.25));
  half4 normal7 = tex2D(sampler3,(ntex3 + 0.5));
  half4 normal8 = tex2D(sampler3,(ntex3 + 0.75));

  // perform DXT5 swizzle and bx2 conversion
  half3 n1 = n0b;
  half3 n2 = ConvertNormal2(normal2);
  half3 n3 = ConvertNormal2(normal3);
  half3 n4 = ConvertNormal2(normal4);  
  
  half3 n5 = ConvertNormal2(normal5);
  half3 n6 = ConvertNormal2(normal6);
  half3 n7 = ConvertNormal2(normal7);
  half3 n8 = ConvertNormal2(normal8);  
  
  half weather  = (0.4 + Weather10*0.8);
  float4 coefs1 = weather*LargeNormalBlend *large;
  float4 coefs2 = weather*SmallNormalBlend *small;

  half3 normallg = n1*coefs1.r + n2*coefs1.g + n3*coefs1.b + n4*coefs1.a;
  half3 normalsm = n5*coefs2.r + n6*coefs2.g + n7*coefs2.b + n8*coefs2.a;

  // Calculate normal disappearing factor (normal map disappears close to shore).
  // The same calculation is done on VS to disappear waves normal modification - see label NORMAL_DISAPPEAR
  //half normalDisappear = min(pow(coastpower, 10), 0.5f);

    //distance lerp
  half3 normal = half3(1.8*(n0a.xy + n0b.xy) + normallg.xy + normalsm.xy, WeatherNormalTex);
  largenormal  = normalize(half3(2*(n0a.xy + n0b.xy), 1));
  
  return normalize(normal);//lerp(normal, half3(0, 0, 1), 1 - saturate(dist*K + Q)));*/
}




//------------------------------------------------------------------------------------
/*
reflection color
*/
//------------------------------------------------------------------------------------
half3 GetReflection(float2 refl, half lerpval)
{
  float size  = (dot(refl.xy, refl.xy));
  refl.xy     = refl.xy*size*0.9f;
  
  half3 c1 = tex2D(sampler4, refl.xy*0.5 + 0.5);
  half3 c2 = tex2D(sampler5, refl.xy*0.5 + 0.5);  
  
  return lerp (c2,c1, lerpval);
}



//------------------------------------------------------------------------------------
/*
refraction color
*/
//------------------------------------------------------------------------------------
float2 GetRefractionMove(half deep01, float3 normal)
{
  //modify texcoords
#ifdef WATER_REAL_REFRACTION
  half invdeep = (1.0 - deep01);
#else
#ifdef ARMA3_SEA_MOD
  half invdeep = (1.025 - deep01);
  invdeep = min(RefractionMoveCoef, invdeep);
#else
  half invdeep = (1.025 - deep01);
#endif
#endif


  return normal.xy*invdeep;  
}



float2 GetRefractionMove(float2 screenpos, float zdepth, half deep01, float3 normal)
{
  float2 move = GetRefractionMove(deep01, normal);

  //test, this must be done to avoid geometry above the water to be refracted too
  float2 texcoord = screenpos * ScreenScale + ScreenOffset;
  float2 coords = texcoord + move;

  float sampleddepth   = tex2D(sampler11, coords).x;

  //get depth in depth buffer and of water
  float pixeld         = 1.0/(sampleddepth + saturate(0.001 - sampleddepth));
  float diff           = pixeld - zdepth;

  //if diff2 < 0 -> new pixel is above water, do not take it, otherwise the pixels above water will move, too

  float coef = saturate(diff*1000);
  return coef*move;//diff2 < 0 ? float2(0, 0) : move;
}





//------------------------------------------------------------------------------------
/*
refraction color
*/
//------------------------------------------------------------------------------------
half3 GetRefraction(float2 screenpos, float3 normal, float3 pointToCam, half deep01, half3 watercolor, float2 move, float2 causticcoord, float zdepth)
{
  float2 texcoord = screenpos * ScreenScale + ScreenOffset;//float2(1.0f/1024, 1.0f/768) + float2(0.5f/1024, 0.5f/768);

  texcoord += move;

  half3 floorcolor = tex2D(sampler10, texcoord);

#ifdef WATER_REAL_REFRACTION
  float deeppower = saturate(pow(deep01, WaterExtFactor));
#else
  //expfactor according the depth and weather
  float expfactor = (1 - deep01*(0.9 + Weather10*0.1));
  float deeppower = saturate(exp(WaterExtFactor*expfactor));
  //half causticpow = deeppower*deeppower;
  //half caustic    = tex2D(sampler6, causticcoord + move*15).r;
#endif


#ifdef ARMA3_SEA_MOD
  // we need to compute x,y,z in camera space
  // we already have z in camera space and x,y in screen space (we can use modified texcoord as [x,y] in screen space)


  // xy coordinates in normalized device state (after perspective projection and perspective division)
  float2 coordNorm = float2(-1 + texcoord.x*2, texcoord.y*2 - 1);
  // xyz coordinates in eye space, divided by z in eye space (because in perspective division w = z in eye space)
  float3 coordEyeNonZ = float3(ProjMatrixInvX * coordNorm.x, ProjMatrixInvY * coordNorm.y, 1);

  // pixel depth from depth buffer 
  float sampleddepth   = tex2D(sampler11, texcoord).x;
  // in depth buffer we have 1/z values
  float pixeld         = 1.0/(sampleddepth + saturate(0.0001 - sampleddepth));

  // object pixel coord in eye space
  float3 pixelEye = coordEyeNonZ * pixeld;
  // water surface pixel coords in eye space
  float3 waterEye = coordEyeNonZ * zdepth; 

  // length of vectors
  float pixelEyeLength = length(pixelEye);
  float waterEyeLength = length(waterEye);

  // distance
  float diff = abs(pixelEyeLength - waterEyeLength);

  // power based on distance
  float power = saturate(MinWaterOpacity + sqrt(diff)*WaterOpacityDistCoef);
  float deeppower2 = 1.0f - power;

  // because we have only near objects in depth buffer, we will fade our computed power to normal power based on distance of object pixel
  deeppower = lerp(max(deeppower2, deeppower), deeppower, saturate((pixelEyeLength - WaterOpacityFadeStart)/WaterOpacityFadeLength));

  // if we are underwater, we want to have power constant
  deeppower = lerp(deeppower, 1.0f - UnderwaterOpacity, IsUnderWaterCoef);

  // modify by angle
  float coef = abs(dot(normal, pointToCam));
  deeppower *= sqrt(coef);
#endif

  return watercolor + floorcolor*deeppower;// + Weather10*causticpow*caustic*0.15;
}



#define DEPTH_MUL   0.01


//------------------------------------------------------------------------------------
/*
refraction color
*/
//------------------------------------------------------------------------------------
float GetDepth(float2 screenpos, float disttopoint, float zdepth, float3 normal, out float2 move)
{
  float2 texcoord     = screenpos * ScreenScale + ScreenOffset;

  float sampleddepth1   = tex2D(sampler11, texcoord).x;

  //get depth of water
  float pixeld1         = 1.0/(sampleddepth1 + saturate(0.001 - sampleddepth1));
  float diff1           = pixeld1 - zdepth;

  //compute move for refraction texture sampling
  float2 currmove       = GetRefractionMove(1 - saturate(DEPTH_MUL*diff1), normal);
  
  //sample again depth map at new position given by move
  float sampleddepth2   = tex2D(sampler11, texcoord + currmove).x;
  float pixeld2         = 1.0/(sampleddepth2 + saturate(0.001 - sampleddepth2));  
  float diff2           = pixeld2 - zdepth;
  
  //if diff2 < 0 -> new pixel is above water, do not take it, otherwise the pixels above water will move, too
  if (diff2 < 0)
  {
    move = 0;
    return diff1*DEPTH_MUL;
  }

  //otherwise, choose which is closer
  float diff;
  if (diff2 < diff1)
  {
    diff = diff1;
    move = currmove;
  } else
  {
    diff = diff2;
    move = GetRefractionMove(1 - saturate(DEPTH_MUL*diff2), normal);
  }
  
//  float diff = diff2;
//  move = currmove;
//  return diff*DEPTH_MUL;
  
  
//  float diff = diff2 < diff1 ? diff1 : diff2;//diff2 < diff1 ? diff1 : diff2;
//  move       = saturate(diff2*1000)*currmove;//diff2 < 0 ? float2(0, 0) : currmove;

  //line equation
  const float mindist = 20.0;
  const float maxdist = 80.0;
  const float K       = 1.0/(mindist - maxdist);
  const float Q       = -maxdist*K;
  float distcoef      = saturate(disttopoint*K + Q);

  diff  = 1 - distcoef*(1 - saturate(diff*DEPTH_MUL));    //lerp(1, saturate(diff*DEPTH_MUL), distcoef);
//  move = 0;//1-distcoef;

  return diff;
}



//------------------------------------------------------------------------------------
/*
reflection color
*/
//------------------------------------------------------------------------------------
half OceanFresnel( half3 norm, half3 dir)
{
#ifdef ARMA3_SEA_MOD
  float coef = abs(dot(dir, norm));
  return 1.0f - coef;
#else
  //  const float4 frp = float4(0.025, 1.0, 1.0, 5.0);
  const float4 frp = float4(0.025, 0.95, 1.0, 5.0);
  return saturate( frp.x + ( 1 - frp.x) * pow((frp.y - frp.z * abs(dot( dir, norm ))), frp.w ) );
#endif
}




//------------------------------------------------------------------------------------
/*
to compute foam visibility -> at min is 0, at edge is 1 and at max is again 0
*/
//------------------------------------------------------------------------------------
float GetFoamFactor(float height, float minheight, float maxheight, float edge, float invmindelta, float invmaxdelta)
{
  float under = saturate((edge - height)*invmindelta);
  float above = saturate((height - edge)*invmaxdelta);
     
  return 1 - (under+above);
}



//------------------------------------------------------------------------------------
/*
to compute foam visibility, from edge to max goes to zero
*/
//------------------------------------------------------------------------------------
float GetFoamFactor(float height, float maxheight, float edge, float invmaxdelta)
{
  float above = saturate((height - edge)*invmaxdelta);
     
  return 1 - above;
}



//------------------------------------------------------------------------------------
/*
get foam
*/
//------------------------------------------------------------------------------------
half GetFoam(sampler2D sampl, float2 ntex1, float2 ntex2, float2 ntex3)
{
  half foam1 = tex2D(sampl, ntex1).x;
  half foam2 = tex2D(sampl, ntex2).x;
  half foam3 = tex2D(sampl, ntex3).x;

  // 1- is here so that if foam texture is missing, we get white foam
  return saturate(foam1 + foam2 + foam3 - 2.4);//2.1875);
}



//------------------------------------------------------------------------------------
/*
foam on the water
*/
//------------------------------------------------------------------------------------
half GetFoam(float normz, float deep, float2 tex1, float2 tex2, float2 tex3)
{
  const float zstart     = 0.7;
  const float zend       = 0.9;
  const float invfoammul = 1.0/(zend - zstart);
  
  //according the z coord
  half  normalfactor  = 1 - saturate((normz - zstart)*invfoammul);
  normalfactor *= 0.5 + normalfactor;
  
  //according the deep
  float deep2      = deep*deep;
  float deepfactor = 0.1 + deep2*deep2; 
  float samplefoam = GetFoam(sampler2, tex1*50.1, tex2*10.2, tex3*0.11);

  return saturate(0.5 - Weather10)*deepfactor*normalfactor*WaterFoamVisibility*samplefoam;
}



//------------------------------------------------------------------------------------
/*
get LdotN
*/
//------------------------------------------------------------------------------------
half GetLdotN(half3 normal, half3 lightdir)
{
  return dot (normal, lightdir)*LDotNScale + LDotNOffset;
}



//////////////////////////////////////////////

half3 GetWaterColor(SWater input, out float2 move, out half3 speccolor, out half3 normal)
{
  //just init
  move = 0;

//return (float3) input.ColorProjZ.w;

  //vector from camera to point, xy = waterplane, and reflection vector
  float3 locpos     = input.VertexPosSkyLerp.xyz;//float3(-stn2lws0.z, stn2lws0.x, stn2lws0.y);
  float disttopoint = sqrt(dot(locpos, locpos));
  float3 pointtocam = locpos/disttopoint;

  // z is vertical in local space (normal map), compute normal map and modify it by normal from vertex and by shore distance
  half3 largenormal, texnormal = ComputeNormal(input.tNormalMap1_2.xy, input.tNormalMap1_2.zw, input.tNormalMap3_LERP.xy, input.tNormalMap3_LERP.z, input.tNormalMap3_LERP.w, largenormal);
  
  half3 vernormal = input.VertexNormalFog.xyz;//half3(specularHalfway.x, specularHalfway.y, specularHalfway.z);
  normal    = ModifyNormal(texnormal, vernormal);
//return normal;

  //reflection vector
  float3 reflvec    = reflect(-pointtocam, normal);
  
  //compute fresnel coef 
  half fresnel      = OceanFresnel(normal, pointtocam);
//return float4((float3) fresnel, 1);
  
  //get reflection from sky
  half3 skyreflection  = GetReflection(reflvec, input.VertexPosSkyLerp.w);
  
  //compute specular and lighting
  float3 lightdir   = PSC_LDirectionGround_DiffuseBack.xyz;
  
  half3 normalspec  = normalize(half3(normal.x, normal.y, SpecNormal));
  half3 specreflvec = reflect(-pointtocam, normalspec);
  
  float spec        = pow(saturate(dot(specreflvec, lightdir)), WeatherSpecularPower);
//  float spec        = pow(saturate(dot(normalspec, normalize(lightdir + pointtocam))), WeatherSpecularPower);
  half LdotN        = GetLdotN(normal, lightdir);
//  return float4((float3) spec, 1);
//  return (float3) dot(normal, lightdir);

  //sum environmental color and sun specular - result is total reflected color
  half3 reflection  = skyreflection*PSC_GlassEnvColor;
  half3 specular    = PSC_Specular.rgb*spec*WeatherSpecular;
//return float4(reflection, 1);

  //refraction
  float2 refrmove = 0;
  half3 refraction;
  
  #ifdef WATER_REAL_REFRACTION
    float deep         = 1 - saturate(GetDepth(input.vPos , disttopoint, input.ColorProjZ.w, normal, refrmove));

    //apply some exponential function, use pow instead of exp, because deep is from 0 to 1 in the usable interval
    //deep              = saturate(pow(1 - deep, 1.2));
    half3 deepcolor  = lerp (input.DeepColor, input.ColorProjZ.rgb, deep*deep);
   #else
    half deep        = input.tWaveMap.w;
    half3 deepcolor  = input.ColorProjZ.rgb;    

    //refrmove = GetRefractionMove(deep, normal);
    refrmove    = GetRefractionMove(input.vPos, input.ColorProjZ.w, deep, normal);
  #endif

  refraction  = GetRefraction(input.vPos, normal, pointtocam, deep, deepcolor, refrmove, input.tNormalMap3_LERP.xy, input.ColorProjZ.w);

  //foam
  half  foamfactor = GetFoam(largenormal.z, input.tWaveMap.w, input.tNormalMap1_2.zw, input.tNormalMap1_2.zw, input.tNormalMap3_LERP.xy);

  
  //final
//  half3 watercolor  = LdotN*(reflection*fresnel + refraction*(1 - fresnel));
//  half3 finalcolor  = lerp(watercolor, PSC_WaveColor.rgb, foamfactor); -> watercolor*(1 - foamfactor) + PSC_WaveColor.rgb*foamfactor;
  half3 watercolor   = LdotN*(reflection*fresnel + refraction*(1 - fresnel));
  half3 speccolor1   = LdotN*specular*fresnel;
  half3 foamcolor    = PSC_WaveColor;
  
  half3 diffusecolor = watercolor*(1 - foamfactor) + foamcolor*foamfactor;
  speccolor          = speccolor1*(1 - foamfactor);
  move               = normal.xy;
  
  return diffusecolor;
}


/*

half4 PSWater(SWater input) : COLOR0
{
  half3 watercolor = GetWaterColor(input);

  return ApplySrcAlpha(half4(ToRTColorSpace(ApplyFog(watercolor, input.VertexNormalFog.w)), 1));
}
*/


//------------------------------------------------------------------------------------
/*
water 
*/
//------------------------------------------------------------------------------------
void PFWater(SWater input, bool sbShadows, float4 tShadowBuffer, float4 vPos, int finalColorType, out half4 oColor)
{
  FOut fout = InitFOut();

  float2 move;
/*  half3 watercolor = GetWaterColor(input, move);
  
  // Calculate the lights
  float shadowcoef = Weather10*0.5 + 0.3;

  fout.light.indirect    = 0;
  fout.light.direct      = 0;
  fout.light.specular    = (1 - shadowcoef)*watercolor;
  fout.color             = 1;
  fout.light.specularEnv = shadowcoef*watercolor;
*/
  half3 normal;
  half3 speccolor, diffusecolor = GetWaterColor(input, move, speccolor, normal);
 
  // Calculate the lights
  float shadowcoef = Weather10*0.6 + 0.2;

  // this  is a very simplified solution.... (we have to use normal.xzy, because normal is in tangent space)
  fout.light.indirect = PointSpotLighting(input.SkinnedPosition.xyz, normal.xzy);

  fout.light.direct      = diffusecolor*shadowcoef;
  fout.light.specular    = /*(1 - shadowcoef)**/speccolor;
  fout.color             = 1;
  fout.light.specularEnv = (1 - shadowcoef)*diffusecolor;

  fout.light.diffuseShadowCoef = 1;

  // Calculate the land shadow
  fout.landShadow = 1.0;
  fout.fog        = input.VertexNormalFog.w;

  float4 shadbuffer = tShadowBuffer;
  shadbuffer.xy = shadbuffer.xy + move*0.005;
  
  CallFinalColor(fout, shadbuffer, vPos.xy, finalColorType, oColor);
}



//------------------------------------------------------------------------------------
/*
to have the same computation in ShoreFoam and Shore
*/
//------------------------------------------------------------------------------------
float GetWaveCoef(float fac)
{
  const float blend    = 0.0;//0.01;
  const float blendmul = 1.0;//1.0/(1.0 - blend);

  return 1.0f - saturate((fac - blend) * blendmul);
}



//------------------------------------------------------------------------------------
/*
water alpha factor according the height
*/
//------------------------------------------------------------------------------------
void GetWaterFactor(float abovewater, float w1height, float w2height, out float waveA, out float waveB)
{
  half waveFactorA = saturate((abovewater + w1height));
  half waveFactorB = saturate((abovewater + w2height));

  // Calculate the water factor (almost discrete output depending on current wave) 
  waveA = GetWaveCoef(waveFactorA);
  waveB = GetWaveCoef(waveFactorB);
}


//------------------------------------------------------------------------------------
/*
water alpha factor according the height
*/
//------------------------------------------------------------------------------------
half GetWaterFactor(float abovewater, float w1height, float w2height)
{
  float waveA, waveB;
  GetWaterFactor (abovewater, w1height, w2height, waveA, waveB);

  // Modify waterFactor - disappear water with height
  //return saturate(saturate(/*sign(abovewater + 0.5)**/1.2f - (abovewater * abovewater)) * max(waveA, waveB));

  float wave = max(waveA,waveB);

  float tozero = saturate(1 - 0.3*abs(abovewater));
  return tozero*wave;//*saturate(max(waveA,waveB)/**(1.0  - abovewater*0.1)*/);
}


//////////////////////////////////////////////

#if _VBS3

float4 PSShore( float4 tNormalMap1_2          : TEXCOORD1,
                float2 tNormalMap3            : TEXCOORD3,
                half4 ambientColor            : TEXCOORD_AMBIENT,
                float4 tFresnelView           : TEXCOORD2,
                half3 specularHalfway         : COLOR0,
                half4 tPSFog                  : COLOR1,
                float4 tHeight                : TEXCOORD0,
                float4 stn2lws0               : TEXCOORD4,
                float4 stn2lws1               : TEXCOORD5,
                float4 stn2lws2               : TEXCOORD_SPECULAR) : COLOR0
{
  // Get the normal
  half3 normal;
  {
    // Sample 3 normals from the source
    half4 normal1 = tex2D(sampler1, tNormalMap1_2.xy);
    half4 normal2 = tex2D(sampler1, tNormalMap1_2.zw);
    half4 normal3 = tex2D(sampler1, tNormalMap3.xy);

    // perform DXT5 swizzle and bx2 conversion
    half4 normalSrc = (normal1+normal2+normal3)*0.333f;
    normalSrc.x += (1-normalSrc.a);
    normalSrc.xy -= 0.5;
    // recalculate z based on xy
    normalSrc.z = 0.333;
    //normal = normalize(normalSrc.xyz);
    //normal = half3(0, 0, 1); // FLY

    // See label NORMAL_DISAPPEAR
    const half maxDisappear = 0.5f;
    normal = normalize(lerp(normalSrc.xyz, half3(0, 0, 1), maxDisappear));
  }

  // Get value from the reflection map
  half3 envColor;
  {
    // Get the LWS position
    float3 lwsPosition = float3(stn2lws0.w, stn2lws1.w, stn2lws2.w);

    // Get the LWS normal
    float3 lwsNormal;
    {
      lwsNormal.x = dot(stn2lws0, normal);
      lwsNormal.y = dot(stn2lws1, normal);
      lwsNormal.z = dot(stn2lws2, normal);
    }

    // Calculate the O vector (the reflection vector)
    float3 o;
    {
      float3 v = normalize(-lwsPosition.xyz);
      o = lwsNormal.xyz * dot(lwsNormal.xyz, v) * 2.0f - v;
    }

    // Modify O vector to favour bounding texels, multiply by constant to omit degenerated texels
    float oSize2 = dot(o.xz,o.xz);
    o.xz = o.xz * oSize2 * 0.95f;

    // Get color from the environmental map
    half3 envColorA = tex2D(sampler4, o.zx * float2(-0.5,+0.5) + 0.5);
    half3 envColorB = tex2D(sampler5, o.zx * float2(-0.5,+0.5) + 0.5);
    envColor = lerp(envColorB, envColorA, tFresnelView.w);
  }

  // Get the fresnel specular coefficient
  half4 fresnelSpecular;
  {
    // Get the specular halfway vector
    half3 specularHalfway = normalize(specularHalfway.rgb);

    // Get the sample coordinates and sample the reflect map
    half u = dot(normal, normalize(tFresnelView));
    half v = dot(normal, specularHalfway);
    fresnelSpecular = tex2D(sampler3, half2(u,v));
  }

  // Sum environmental color and sun specular - result is total reflected color
  half3 reflectedColor = envColor * PSC_GlassEnvColor + fresnelSpecular.rgb * PSC_Specular;

  // Calculate the water factor (1 - on wave, 0 - biggest distance from wave)
  half waveFactorA = saturate(tHeight.w + tHeight.x);
  half waveFactorB = saturate(tHeight.w + tHeight.y);

  // Calculate the water factor (almost discrete output depending on current wave)
  half waterFactorA = 1.0f - saturate((waveFactorA - 0.80f) * 10.0f);
  half waterFactorB = 1.0f - saturate((waveFactorB - 0.80f) * 10.0f);

  // Modify waterFactor - disappear water with height
  half waterFactor = saturate(saturate(1.2f - /*abs*/(tHeight.w * tHeight.w)) * max(waterFactorA, waterFactorB));


  half3 color = lerp(ambientColor.rgb, reflectedColor, fresnelSpecular.a) * waterFactor;
  half alpha = waterFactor * ambientColor.a;
  half4 base = half4(color,alpha);
  float4 result = base;
  return ApplySrcAlpha(half4(ToRTColorSpace(ApplyFog(result.rgb, tPSFog)),result.a));
}

#else


FOut PFShore(SShore input, bool sbShadows, float4 ScreenPos : SV_POSITION)
{
  float3 locpos     = input.VertexPos_SkyLerp.xyz;//float3(-stn2lws0.z, stn2lws0.x, stn2lws0.y);
  float disttopoint = sqrt(dot(locpos, locpos));
  float3 pointtocam = locpos/disttopoint;

  // z is vertical in local space (normal map)
  half3 largenormal, normal = ComputeNormal(input.tNormalMap1_2.xy, input.tNormalMap1_2.zw, input.tNormalMap3.xy, input.tNormalMap3.z, input.tNormalMap3.w, largenormal);

  //compute fresnel coef 
  half fresnel      = OceanFresnel(normal, pointtocam);

  //get reflection from sky
  float3 reflvec    = reflect(-pointtocam, normal);  
  
  half3 skyreflection  = GetReflection(reflvec, input.VertexPos_SkyLerp.w);

  //compute specular and lighting
  float3 lightdir   = PSC_LDirectionGround_DiffuseBack.xyz;
  
  half3 normalspec  = normalize(half3(normal.x, normal.y, SpecNormal));
  half3 specreflvec = reflect(-pointtocam, normalspec);
  
  float spec        = pow(saturate(dot(specreflvec, lightdir)), WeatherSpecularPower);
  half LdotN        = GetLdotN (normal, lightdir);

  // Sum environmental color and sun specular - result is total reflected color
  half3 reflectedColor  = (skyreflection*PSC_GlassEnvColor + spec*PSC_Specular*WeatherSpecular);

  float2 move           = GetRefractionMove(ScreenPos, input.CoastColorProjZ.w, 1, normal);

  #ifdef WATER_REAL_REFRACTION
  half3 refraction      = GetRefraction(ScreenPos, normal, pointtocam, 1, input.CoastColorProjZ.rgb*CoastColorFactor, move, input.tNormalMap3.xy);
 
    const float mindist = 20.0; 
    const float maxdist = 80.0;
    const float K       = 1.0/(mindist - maxdist);
    const float Q       = -maxdist*K;
    float distcoef      = saturate(disttopoint*K + Q);
 
    refraction = lerp(input.CoastColorProjZ.rgb, refraction, distcoef);
  #else  
  half3 refraction      = GetRefraction(ScreenPos, normal, pointtocam, 1, input.CoastColorProjZ.rgb, move, input.tNormalMap3.xy, input.CoastColorProjZ.w);
  #endif
  
  
  half3 outcolor        = LdotN*(fresnel*reflectedColor + (1-fresnel)*refraction); 

  // Calculate the water factor (1 - on wave, 0 - biggest distance from wave)
  float height  = input.Wave_X_X_Height_Fog.z;
  float fog     = input.Wave_X_X_Height_Fog.w;
  
//  half waterFactor = GetWaterFactor(height, input.Wave_X_X_Height_Fog.x, input.Wave_X_X_Height_Fog.y);

  //water
  const float MAX       = 1.3;
  const float EDGE      = 0.75;
  const float INVDELTA  = 1.0/(MAX - EDGE);

  float fac1 = GetFoamFactor(height + 0.8*input.Wave_X_X_Height_Fog.x, MAX, EDGE, INVDELTA);
  float fac2 = GetFoamFactor(height + 0.8*input.Wave_X_X_Height_Fog.y, MAX, EDGE, INVDELTA);
  float fac  = max(fac1, fac2);
 
  float waterFactor = fac*fac;
 
  //coast foam
  half foam = GetFoam(sampler2, input.tNormalMap1_2.zw*20, input.tNormalMap1_2.zw*10, input.tNormalMap3.xy);
  
  const float FMIN         = 0.5;
  const float FMAX         = 1.5;
  const float FEDGE        = 1.25;
  const float FINVMINDELTA = 1.0/(FEDGE - FMIN);
  const float FINVMAXDELTA = 1.0/(FMAX - FEDGE);

  float foamfac1 = GetFoamFactor(height*1.5 + 0.5*input.Wave_X_X_Height_Fog.x, FMIN, FMAX, FEDGE, FINVMINDELTA, FINVMAXDELTA);
  float foamfac2 = GetFoamFactor(height*1.5 + 0.5*input.Wave_X_X_Height_Fog.y, FMIN, FMAX, FEDGE, FINVMINDELTA, FINVMAXDELTA);
  
  float foamfac    = max(foamfac1, foamfac2);
  foamfac          = foamfac*foamfac;
  float foamFactor = 2*foamfac*foam;
  
  //water foam
  half  waterfoam = GetFoam(largenormal.z, 1, input.tNormalMap1_2.zw, input.tNormalMap1_2.zw, input.tNormalMap3.xy);

  //GetFoam(FoamNormalVertexZ + normal.z*(1 - FoamNormalVertexZ), 1, input.tNormalMap1_2.xy, input.tNormalMap1_2.zw, input.tNormalMap3.xy);

  //collect foam
  foamFactor = max(foamFactor, waterfoam);
  

  float shadowcoef  = Weather10*0.5 + 0.3;
  float3 finalcolor = lerp(outcolor, PSC_WaveColor.rgb, foamFactor);
  
  // Calculate the lights
  FOut fout = InitFOut();  
  
  fout.light.indirect           = 0;
  fout.light.specular           = waterFactor*finalcolor;
  fout.light.diffuseShadowCoef  = 1;
  fout.light.specularEnv        = 0;

  float underwater              = height*1.5 + 0.7*input.Wave_X_X_Height_Fog.x < -0.2 ? 0 : 1;
  fout.color                    = half4((half3) 0, waterFactor*underwater*fog);//0.03);
  fout.fog                      = fog;
  fout.landShadow               = 1;
  
  return fout;
}

#endif

//////////////////////////////////////////////


FOut PFShoreFoam(SShore input, bool sbShadows, float4 ScreenPos : SV_POSITION)
{
  // Get the foam
  half foam = GetFoam(sampler2, input.tNormalMap1_2.xy, input.tNormalMap1_2.zw, input.tNormalMap3.xy);

  float height  = 2.0*input.Wave_X_X_Height_Fog.z;
  float fog     = input.Wave_X_X_Height_Fog.w;

 
  //TEST of wave
  float heightA = (height + 0.5*input.Wave_X_X_Height_Fog.x);  
  float heightB = (height + 0.5*input.Wave_X_X_Height_Fog.y);  
  
  const float MIN         = -1;
  const float MAX         = 1.5;
  const float EDGE        = 1.0;
  const float INVMINDELTA = 1.0/(EDGE - MIN);
  const float INVMAXDELTA = 1.0/(MAX - EDGE);

  float foamfac1 = GetFoamFactor(heightA, MIN, MAX, EDGE, INVMINDELTA, INVMAXDELTA);
  float foamfac2 = GetFoamFactor(heightB, MIN, MAX, EDGE, INVMINDELTA, INVMAXDELTA);
  
  float foamfac  = max(foamfac1, foamfac2);
  foam           = foam*0.5 + 0.5;

  float foamFactor = pow (foam*foamfac, 4);
  
  FOut fout = InitFOut();

  // Calculate the lights
  fout.light.indirect           = 1;
  fout.light.diffuseShadowCoef  = 1;
  fout.light.specularEnv        = 0;  
  fout.color                    = half4(PSC_WaveColor.rgb, 0);//foamFactor*fog);
  //fout.color                    = half4((half3)foamFactor, 1);  
  fout.fog                      = fog;
  fout.landShadow               = 1;

  return fout;
}

//////////////////////////////////////////////

FOut PFShoreWet(SShoreWet input, bool sbShadows, float4 ScreenPos : SV_POSITION)
{
  // Calculate the wet factor (darkenning of shore disappearing with distance above water)
  float height = input.Wave_X_X_Height_Fog.z;  
  float fog    = input.Wave_X_X_Height_Fog.w;  
  
  float wetFactor = 1.0f - saturate(0.5f*abs(height)); //0.3*saturate(1.2 - 0.3*abs(height));

  // Fill out the FOut structure
  FOut fout = InitFOut();
  fout.light = GetLightSimple((half3) 1, (half3) 0, (half3) 0);
  
  // Calculate the color
  fout.color = half4(0, 0, 0, wetFactor*fog);

  // Calculate the fog
  fout.fog = fog;

  // Calculate the land shadow
  fout.landShadow = 1;

  return fout;
}






////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSThermalWater(SWater input) : COLOR0
{
  float fog = input.VertexNormalFog.w;
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(15.0f, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);
  return ApplySrcAlpha(half4(ToRTColorSpace(ApplyFog(half3(rgbEncodedTemperature), fog)), 1));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSWaterSimple( float4 basicColor:TEXCOORD_AMBIENT, float3 reflectionHalfway:TEXCOORD_SPECULAR,
                     float2 tWaveMap:TEXCOORD0, float2 tNormalMap1:TEXCOORD1,
                     float3 tFresnelView:TEXCOORD2,
                     float3 specularHalfway : COLOR0,
                     half tPSFog           : COLOR1,
                     float2 tNormalMap2:TEXCOORD4, float2 tNormalMap3:TEXCOORD3) : COLOR0
{
  // z is vertical in local space (normal map)
  float4 normal1 = tex2D(sampler1,tNormalMap1);
  float4 normal2 = tex2D(sampler1,tNormalMap2);
  float4 normal3 = tex2D(sampler1,tNormalMap3);

  // x is sum of r + (1-a)
  /*
  // perform DXT5 nomap format 1-a->r swizzle and normalize 
  float4 sumNormals = normal1+normal2+normal3;
  sumNormals.x += (3-sumNormals.a);
  float3 normal = normalize(sumNormals.rgb-1.5);
  */

  // perform DXT5 swizzle and bx2 conversion
  float4 normalSrc = (normal1+normal2+normal3)*0.333f;
  normalSrc.x += (1-normalSrc.a);
  normalSrc.xy -= 0.5;
  // recalculate z based on xy
  normalSrc.z = sqrt(1-dot(normalSrc.xy,normalSrc.xy));

  float3 normal = normalSrc.xyz;

  reflectionHalfway = reflectionHalfway*2-1;

  specularHalfway = normalize(specularHalfway);

  float u = dot(normal,tFresnelView);
  float v = dot(normal,specularHalfway);

  float4 fresnelSpecular = tex2D(sampler3,float2(u,v)); // Reflect map

  float cosReflectHalf = max(dot(normal,reflectionHalfway), 0);

  // full ground reflection at value cos(45deg) = 0.70
  // we would like to know cos (2*alfa)
  // cos 2x = 2 cos^2(x) - 1
  float reflected = saturate(2*cosReflectHalf*cosReflectHalf-1);
  float3 reflectedColor = lerp(PSC_GroundReflColor,PSC_SkyReflColor,reflected) ;

  reflectedColor += fresnelSpecular.rgb*PSC_Specular.rgb;

  float wave = tex2D(sampler0,tWaveMap).x; // Wave map
  float3 bodyColor = basicColor;

  float3 color = lerp(bodyColor,reflectedColor,fresnelSpecular.a);


  float alpha = max(basicColor.a, fresnelSpecular.a);

  return ApplySrcAlpha(half4(ToRTColorSpace(ApplyFog(color, tPSFog)),alpha));
}

float4 PSThermalWaterSimple(half tPSFog : COLOR1) : COLOR0
{
  float3 rgbEncodedTemperature;
  EncodeTemperatureToRGB(15.0f, rgbEncodedTemperature.r, rgbEncodedTemperature.g, rgbEncodedTemperature.b);
  return ApplySrcAlpha(half4(ToRTColorSpace(ApplyFog(half3(rgbEncodedTemperature), tPSFog)), 1));
}

//////////////////////////////////////////////

/// shader to query brightness
float4 PSPostProcessQuery(VSPP4T_OUTPUT input) : COLOR
{
  float value = DecodeApertureChange(tex2D(sampler0, input.TexCoord0).x); // sampling 1 x 1
  // we need to derive input texture position from input U
  float u = input.TexCoord0.x;
  //float pixel = 
  //float pixel = PSC_QueryDot.x; // pixel index, 0..NBrightQueries-1
  float thold = tex2D(sampler1,u); // sampling NBrightQueries x 1
  clip(value-thold);
  return float4(value.x,thold,0,1);
}

float4 PSPostProcessQuerySimple(VSPP4T_OUTPUT input) : COLOR
{
  float value = tex2D(sampler0, input.TexCoord0).x;
  clip(value-PSC_QueryDot.w);
  return float4(value.xxx,1);
}

half3 ToneMapping(half3 c, half intensity, half3 maxAvgMin)
{
  // apply tone-mapping
  // tone mapping should never extend dynamic range
  // we also do not want to extend it too much, we want to get some overbright
  half oMax = min(max(maxAvgMin.r,1),2);
  // never needed to compress more than necessary
  // we might precalculate some of the following expression in some texture
  half3 result = c / (1+c*(oMax-1)/oMax);

  // IN DX11 we need to perform srgb conversion here
  // For reason see news:ijgrrt$cd0$1@new-server.localdomain
  return pow(result, 1/2.2f);

  //return c / (1+c);
  //return c;
  //return maxAvgMin;
  //return c/max(maxAvgMin.r,0.01);
}

/// aperture (aperture value is computed on GPU)
float ApertureControl(float texValue)
{
  // sample aperture modification texture
  // value is stored as 0.5x, so that it can be higher than 1
  float apertureMod = DecodeApertureChange(texValue);
  return PSC_HDRMultiply.x*apertureMod;
}

//! Function to return brightness of the color
half Brightness(half3 color)
{
  return dot(color, half3(0.299, 0.587, 0.114));
}

//! Simple pixel shader designed for flare drawing
float4 PSNonTLFlare(SNonTL input) : COLOR
{
  // Get the brightness coefficient upon measured visibility of the light source
  half brightness = Brightness(tex2D(samplerLightIntensity, half2(0.5, 0.5)).rgb);
  half bCoef = saturate((brightness - 0.5) * 10.0);
  //half bCoef = (brightness > 0.1) ? 1 : 0;

  // Get the aperture value
  float aperture = ApertureControl(tex2D(sampler14,float2(0,0)).x);

  // Calculate original color and alpha
  half4 cOriginal = tex2D0(input.diffuseMap.xy);
  cOriginal.a = cOriginal.a*input.ambientColor.a*bCoef;
  cOriginal.rgb = cOriginal.rgb*input.ambientColor;

  // Include aperture
  half3 cAfterAperture = cOriginal.rgb * aperture;

  // read min/max/avg, transform it into the new color space
  half3 maxAvgMin = tex2D(sampler13,float2(0,0)) * aperture;
  return half4(ToneMapping(cAfterAperture, dot(cAfterAperture, PSC_RgbEyeCoef.rgb),maxAvgMin)*cOriginal.a,0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// CALM WATER
////////////////////////////////////////////////////////////////////////////////////////////////////

// SAMPLER USAGE FOR CALM WATER
//-----------------------------
// sampler0 ...normal map A
// sampler1 ...normal map B
// sampler2 ...height map A+B
// sampler3 ...detail noise
// sampler4 ...foam
// sampler5 ...caustics
// sampler6
// sampler7 ... sky plane

// sampler8
// sampler9
// sampler10 ...RenderTargetCopy
// sampler11 ...Depth

void PFCalmWater0( SCalmWaterVs2Ps i, inout SCalmWaterPFT pft )
{
  pft.hMapA01 = tex2D( sampler2, i.tc1.xy ).ag;
  pft.shadow = 1;
  //float4 tcShadow; // texture coords for shadowmap
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PFCalmWater1( SCalmWaterVs2Ps i, SCalmWaterPFT pft, float4 vpos )
{
  half4 final = 1;
  half alpha;
  float2 tc0 = i.tc0.xy;
  float2 tc1 = i.tc1.xy;
  float2 tc2 = i.tc1.zw;
  float2 tc1D = i.tc2.xy;
  float2 tc2D = i.tc2.zw;
  
  half surfaceShadow = pft.shadow;
  
  // view position
  float3 posV = i.tc3.xyz;
  float3 posO = float3( i.tc4.w, i.tc5.w, i.tc6.w );
  //
  //float3 dirFromCamDO = i.tc5.xyz; // dir from cam to screen (not normalized) in object space
  float3 camDirO = normalize( i.tc5.xyz );
  float3 camPosO = i.tc6.xyz; // camera position in object space
  half3 dirToCam = normalize( -posV );
  float distFromCam = length( posV );
  half3 dirToCamO = normalize( i.tc4.xyz );
  
  // TODO: optimize this if possible
  float rd = 1.0 / dot( camDirO.xyz, -dirToCamO.xyz );
  float3 dirFromCamDO = -rd * dirToCamO.xyz;
  
  half4 blendsA = PSC_CalmWaterPars1[2];
  half4 blendsB = PSC_CalmWaterPars1[3];
  
// PARALAX MAPPING
#ifdef CALMWATER_PARALLAX
  half4 hMapA;
  hMapA.rg = pft.hMapA01;
  hMapA.ba = tex2D( sampler2, tc1 + 0.5 ).ga;
  half hA = dot( blendsA, hMapA );
  float2 tc1d = ( hA - 0.5 ) * dirToCamO.xz * PSC_CalmWaterPars1[10].z;
  half4 hMapB;
  hMapB.rg = tex2D( sampler2, tc2 ).ga;
  hMapB.ba = tex2D( sampler2, tc2 + 0.5 ).ga;
  half hB = dot( blendsB, hMapB );
  float2 tc2d = ( hB - 0.5 ) * dirToCamO.zx * PSC_CalmWaterPars1[10].w;
  tc1 += tc1d;
  tc2 += tc2d;
#endif // CALMWATER_PARALLAX
  
// MAIN WAVES NORMAL BLENDS
  half2 texA0 = tex2D( sampler0, tc1 ).ag;
  half2 texA1 = tex2D( sampler1, tc1 ).ag;
  half2 texA2 = tex2D( sampler0, tc1 + 0.5 ).ag;
  half2 texA3 = tex2D( sampler1, tc1 + 0.5 ).ag;
  half2 texB0 = tex2D( sampler0, tc2 ).ag;
  half2 texB1 = tex2D( sampler1, tc2 ).ag;
  half2 texB2 = tex2D( sampler0, tc2 + 0.5 ).ag;
  half2 texB3 = tex2D( sampler1, tc2 + 0.5 ).ag;
  half2 norm1 = texA0.rg * blendsA.x + texA1.rg * blendsA.y + texA2.rg * blendsA.z + texA3.rg * blendsA.w;
  half2 norm2 = texB0.gr * blendsB.x + texB1.gr * blendsB.y + texB2.gr * blendsB.z + texB3.gr * blendsB.w;

// DETAIL MAP
  half4 texD1 = tex2D( sampler3, tc1D );
  half4 texD2 = tex2D( sampler3, tc2D );
  //texD1 = 0.5;
  //texD2 = 0.5;
  half4 blendsD = PSC_CalmWaterPars2[0];
  half2 detail0;
  detail0.xy = blendsD.x * texD1.xy + blendsD.y * texD1.zw
    + blendsD.z * texD2.yx + blendsD.w * texD2.wz;// + PSC_CalmWaterPars[7].w;
    
  // vanish for edge foam (also used for detail waves)
  half efoamVanish = saturate( 1.2 - PSC_CalmWaterPars2[3].w * distFromCam );
  //detail0.xy = lerp( detail0.xy, half2( PSC_CalmWaterPars1[7].z, PSC_CalmWaterPars1[7].z ), efoamVanish );
  
  half3 norm0;
  norm0.xy = norm1 + norm2 + detail0 + PSC_CalmWaterPars1[7].w;
  norm0.z = 1.0; //PSC_CalmWaterPars1[7].z;
  
  half3 norm = normalize( norm0.xzy ); // swizzle (verical is y-axis)
  //norm = half3( 0, 1, 0 ); //<<< default normal TEST
  
  half fresnel = WaterFresnel( norm, dirToCamO, PSC_CalmWaterPars2[1].xyzw );
  //half3 reflVec = reflect( -dirToCam, norm );
  half3 reflVec = reflect( -dirToCamO, norm );
  
// ENVIRONMENTAL REFLECTION
  const half kr = 0.50;
  half2 skyTc = saturate( reflVec.xy * half2( kr, -kr ) + half2( kr, kr ) );
  half3 sky = tex2D( sampler7, skyTc );
  //sky.rgb = lerp( sky.rgb, AplyGamma22( sky.rgb ), SwitchesL.w ); //<< Gamma22??
  
// DEPTH COMPS.
// outs:
  half h; // water height above bottom
  half l; // distance under water
  half d; // bottom depth
  const float depth0 = 20.0f; // konstant depth for distant water
  
  float2 invViewPortXY = PSC_CalmWaterPars1[4].zw;
  float2 tcS = vpos.xy * invViewPortXY + invViewPortXY * float2( 0.5, 0.5 ); // screen-space coords
  float lc = distFromCam; // distance from camera
  float ds = i.tc3.w; // depth of surface
  float depthC = depth0 + ds;
  //float kd = saturate( ds * ( 1.0 / 20 ) - 3.0 ); // depth koef
  float d1r = tex2D( sampler11, tcS ).r;// linear screen depth in world-space coords
  float d1 = 1 / ( d1r + saturate( 0.001 - d1r ) );
  //d1 = lerp( d1, depthC, kd ); // depth texture error correction
  float l1 = d1 - ds;
  float2 dtcS = norm.xz * ( saturate( l1 ) * 0.002 + 0.05 );
  float d2r = tex2D( sampler11, tcS + dtcS ).r;
  float d2 = 1 / ( d2r + saturate( 0.001 - d2r ) );
  //d2 = lerp( d2, depthC, kd ); // depth texture error correction
  float l2 = d2 - ds;
  //half kRErr = saturate( sign( d2 ) ); // correction coef. of edge errors
  half kRErr = saturate( l2 * 100 ); // correction coef. of edge errors
  half3 refrTex;
#ifdef CALMWATER_SIMPLE_REFRACT
  refrTex = half3( 38/255.f, 34/255.f, 26/255.f );
#else
  refrTex = tex2D( sampler10, tcS + kRErr * dtcS ).rgb;
#endif
  d = lerp( d1, d2, kRErr );
  //l = lerp( l1, l2, kRErr ); //saturate( dr ) ); // correction of edge errors
  l = d - ds;
  
  //float3 bottomPosV = posV.xyz * d / ds;
  float3 bottomPosO = dirFromCamDO.xyz * d + camPosO.xyz;
  h = posO.y - bottomPosO.y;
  
// REFRACTION
  half3 refr;
  half deepLight;
  half deepFog;
  
// EDGE FOAM - blend factors
  half2 tcEF = tc0.xy * PSC_CalmWaterPars2[2].xy + norm.xz * PSC_CalmWaterPars2[2].zw;
  half4 texEFoam = tex2D( sampler4, tcEF );
  //half sharpAngleFilter = saturate( -15.4 * dirToCam.y + 1.54 );
  half foamD = max( l * 0.3, h ); //( h * 0.2 + l * 0.8 ); // behind foam distance in water factor
  half efoamBlend; // color blend factor
  half efoamA; // material alpha factor
  efoamBlend = texEFoam.g + PSC_CalmWaterPars2[4].x - foamD * PSC_CalmWaterPars2[4].y;
  half efoamF = saturate( PSC_CalmWaterPars2[4].z - foamD * PSC_CalmWaterPars2[4].w );
  //half efoamVanish = saturate( 1.2 - PSC_CalmWaterPars2[3].w * lc );
  //efoamVanish = 0;//<<TEST
  efoamF *= efoamF;
  efoamA = saturate( efoamBlend - efoamF );
  efoamA *= efoamVanish; // distance vanish param
  efoamBlend = saturate( efoamBlend ) * efoamVanish;
  

// LIGHTING part 1
  half3 refl;
  alpha = saturate( l * 4.0 - 0.2 ); // soft edges
  half3 skyLightColor = PSC_CalmWaterPars2[10].rgb; //half3( 0.3, 0.4, 0.5 );
  half3 mainLightDir = PSC_CalmWaterPars2[11].xyz; //PSC_WLight.xyz; // PSC_WLight ..Main light direction in world coordinates
  half3 mainLightColor = PSC_CalmWaterPars2[12].rgb; //half3( 0.9, 0.8, 0.7 );
  half3 mainLightSpecular = PSC_CalmWaterPars2[13].rgb; //half3( 0.9, 0.8, 0.7 );
  half specularPower = PSC_CalmWaterPars2[10].w;
  half3 waterColor = half3( PSC_CalmWaterPars2[11].a, PSC_CalmWaterPars2[12].a, PSC_CalmWaterPars2[13].a );  //half3( 0.35, 0.41, 0.44 );
  sky.rgb *= skyLightColor.rgb;
  half nDotL = dot( norm.xyz, mainLightDir.xyz );
  half3 reflMain;
  reflMain = pow( saturate( dot( reflVec.xyz, mainLightDir.xyz ) ), specularPower ) * surfaceShadow;
  reflMain = mainLightSpecular.rgb * saturate( reflMain - efoamBlend * 1.6 ); // no refl on foam
  refl = sky.rgb + reflMain.rgb;
  
// CAUSTICS
  // caustics attenuation
  half cattMul = PSC_CalmWaterPars2[5].x; //0.1;
  half cattAdd = PSC_CalmWaterPars2[5].y; //0.9;
  half catt = saturate( cattMul * h + cattAdd );
  // caustics focus
  half cfocusMul = PSC_CalmWaterPars2[5].z; //0.5;
  half cfocusAdd = PSC_CalmWaterPars2[5].w; //1.2;
  half cfocus = saturate( cfocusMul * h + cfocusAdd );
  // caustics texcoords
  //float tcs0 = 1;
  float2 ksin;
  float kPI2 = 1.2 * 6.28f;
  float2 tcc0 = ( bottomPosO.xz + h * mainLightDir.xy ) * PSC_CalmWaterPars2[7].x; //0.15;
  half2 ctex2 = tex2D( sampler2, tcc0 ).ga; // using height map as anim map
  ksin.x = PSC_CalmWaterPars2[6].x * sin( PSC_CalmWaterPars2[6].z + ctex2.x * kPI2 ); // 0.021, 3.1
  ksin.y = PSC_CalmWaterPars2[6].y * sin( PSC_CalmWaterPars2[6].w + ctex2.y * kPI2 ); // 0.027, 4.3
  float2 tcc1 = tcc0.xy + ctex2.xy * ksin;
  half2 ctex1 = tex2D( sampler5, tcc1 ).ga;
  half caus = lerp( ctex1.g, ctex1.r, cfocus );
  //caus *= maskD0; ///
  half dca = dot( mainLightDir.xyz, dirToCamO.xyz ) * PSC_CalmWaterPars2[7].z + PSC_CalmWaterPars2[9].w;
  caus *= saturate( dca ) * saturate( catt ) * PSC_CalmWaterPars2[7].y;
  
// NEW DEEP SCATTER
  // amount of light (scattered, caustics...) at bottom (multiplied by current render color eventually)
  half3 lightAtBottom;
  // color and scattered light in water volume
  half3 volumeColor;
  // coef for volume color blending
  half volumeBlend;
  // sum of light from above the surface (sun + skylight)
  half3 lightSum;
  //
  half4 waterFogPars = PSC_CalmWaterPars1[8];
  half4 waterScatterPars = PSC_CalmWaterPars1[9];
  //
  lightSum = ( skyLightColor.rgb + mainLightColor.rgb ) * waterFogPars.w;
  //volumeBlend = pow( saturate( waterFogPars.y - l * waterFogPars.x ), waterFogPars.z );
  volumeBlend = saturate( waterFogPars.y - l * waterFogPars.x );
  volumeBlend *= volumeBlend; volumeBlend *= volumeBlend; // pow 4
  // scatter coef at bottom depth
  half lightSc0 = saturate( waterScatterPars.y - h * waterScatterPars.x );
  lightSc0 *= lightSc0; lightSc0 *= lightSc0; // pow 4
  //half lightSc0 = pow( saturate( waterScatterPars.y - h * waterScatterPars.x ), waterScatterPars.z );
  // scatter coef at cam depth (underwater only - not used in "arma")
  half lightSc1 = 1;
  //half lightSc1 = pow( saturate( waterScatterPars.y - 0 * waterScatterPars.x ), waterScatterPars.z );
  lightAtBottom.rgb = lightSc0 * ( 0.5 + caus ) * lightSum;
  volumeColor = waterColor.rgb * lerp( lightSc0, lightSc1, waterScatterPars.w ) * lightSum;
  refr = lerp( volumeColor.rgb, refrTex.rgb * lightAtBottom.rgb, volumeBlend );
  
// EDGE FOAM
  // compute texcoord from base wrap and normals
  half3 efoamCol = PSC_CalmWaterPars2[3].rgb;
  half3 efoamDiffSky = ( norm.y * 0.5 + 0.5 ) * skyLightColor.rgb;
  half3 efoamDiffMain = mainLightColor.rgb * saturate( nDotL * 0.6 + 0.3 ) * surfaceShadow;
  half3 efoamFinal = efoamCol.rgb * ( efoamDiffSky + efoamDiffMain.rgb );
  
  // final water reflection and refraction
  final.rgb = ( 1.0 - fresnel ) * refr.rgb + fresnel * refl.rgb;
  
  // add foam to final
  final.rgb = lerp( final.rgb, efoamFinal, efoamBlend );
  alpha = max( alpha, efoamA );
  
  //final.rgb = efoamFinal * efoamBlend;//<<< FOAM TEST
  
  final.a = alpha;
  final.rgb = ApplySrcAlpha( final.rgb, final.a );
  
#ifdef CALMWATER_REFRACT_DEBUG
  final.rgb = tex2D( sampler10, tcS ).rgb;
  final.b += 0.2;
  final.a = 1.0;
#endif

  //final.rgb = caus * 0.5; final.a = 1; //<<< CAUSTICS TEST

  half fogLerpFactor = i.tc0.w;
  final.rgb = ApplyFog( final.rgb, fogLerpFactor, final.a );
  
// TESTING >>>>>>>>>>>>>>>>>>>>>>

  /*final.r = PSC_CalmWaterPars2[15].y;
  final.g = PSC_CalmWaterPars2[15].y * 5;
  final.b = -PSC_CalmWaterPars2[15].y * 5;
  final.rgb = saturate( final.rgb );
  final.a = 1.0;*/
  //final.rgb = pow( final.rgb, 2.2 ); //<<< linear to sRGB
  
  return final;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//! function to retrieve shadow coef 
//half GetShadowBasicSSSM( SSkin i, half diffShadowCoef )
half GetShadowBasicSSSMNoise( float4 tShadowBuffer, float2 vpos, half4 landShadow, half diffShadowCoef, half4 noise )
{
  half shadowCoef;
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half disappear = saturate( (tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);
  half4 tcs = half4( ( vpos.xy + half2( 0.5, 0.5) ) * PSC_InvW_InvH_X_X.xy, 0, 1 );
  half2 ks = ( disappear + 0.1 ) * PSC_InvW_InvH_X_X.xy;
  half2 ds1 = noise.xy * ks.xy;
  half2 ds2 = noise.zw * half2( ks.x, -ks.y );
#ifdef _XBOX
  shadowCoef = disappear + tex2Dlod(samplerShadowMap, float4((vpos.xy + PSCGPU_TOffX_TOffY_X_X[0].xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - disappear);
#else
  shadowCoef  = tex2Dlod( samplerShadowMap, tcs + half4( ds1.xy, 0, 0 ) ).r;
  shadowCoef += tex2Dlod( samplerShadowMap, tcs - half4( ds1.xy, 0, 0 ) ).r;
  shadowCoef += tex2Dlod( samplerShadowMap, tcs + half4( ds2.xy, 0, 0 ) ).r;
  shadowCoef += tex2Dlod( samplerShadowMap, tcs - half4( ds2.xy, 0, 0 ) ).r;
  shadowCoef *= 0.25;
  shadowCoef *= shadowCoef;
  shadowCoef = disappear + shadowCoef * ( 1.0 - disappear );
  //shadowCoef = disappear + tex2Dlod(samplerShadowMap, float4((vPos.xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r * (1 - disappear);
#endif
  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - diffShadowCoef) * disappear;  // fout.light.diffuseShadowCoef
  // Involve land shadow to shadow coefficient
  shadowCoef *= landShadow.a;
  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);
  return shadowCoef;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//! function to retrieve shadow coef
half GetShadowSRDefaultNoise( float4 tShadowBuffer, float2 vpos, half4 landShadow, half diffShadowCoef, half4 noise )
{
  // 
  half noisiness = 2.0;
  // Precomputed constants related to texture size
  const float texsize = PSC_SBTSize_invSBTSize_X_AlphaMult.x;
  const float invTexsize = PSC_SBTSize_invSBTSize_X_AlphaMult.y;
  const half shadowsZHalf = PSC_Shadow_Factor_ZHalf.w;
  half disappear = saturate((tShadowBuffer.w - shadowsZHalf) / shadowsZHalf);

  //half4 tcs = half4( ( vpos.xy + half2( 0.5, 0.5) ) * PSC_InvW_InvH_X_X.xy, 0, 1 );
  half ks = invTexsize * noisiness;
  half2 ds1 = noise.xy * ks;
  half2 ds2 = noise.zw * half2( ks, -ks );

  float4 shadowDepth = float4(
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + half4( ds1.xy, 0, 0 ) ).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw - half4( ds1.xy, 0, 0 ) ).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw + half4( ds2.xy, 0, 0 ) ).r,
    tex2Dlod(samplerShadowMap, tShadowBuffer.xyzw - half4( ds2.xy, 0, 0 ) ).r
  );
  // Get 4 shadow/not shadow values
  float4 sc = shadowDepth >= tShadowBuffer.z;
  // Use weights to get the shadow coefficient
  //half shadowCoef = dot(sc, weights);
  half shadowCoef = dot( sc, 0.25h );
  shadowCoef *= shadowCoef;
  shadowCoef = disappear + shadowCoef * ( 1 - disappear );
  // Involve diffuse map content (use the diffuse shadow map where no shadow is present)
  shadowCoef -= (1 - diffShadowCoef) * disappear;
  // Involve land shadow to shadow coefficient
  shadowCoef *= landShadow.a;
  // Control shadow intensity via PSC_Shadow_Factor_ZHalf.x
  shadowCoef = 1 + PSC_Shadow_Factor_ZHalf.x * (shadowCoef - 1);
  return shadowCoef;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSCalmWater( SCalmWaterVs2Ps i, float4 vpos : VPOS ) : COLOR0
{   
  half4 final;
  SCalmWaterPFT pft;
  PFCalmWater0( i, pft );
  pft.shadow = 1;
  final = PFCalmWater1( i, pft, vpos );
  //final.b += 0.2; // TEST
  return final;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSSRCalmWater_Default( SCalmWaterVs2Ps i, float4 vpos : VPOS ) : COLOR0
//float4 PSSRCalmWater_nVidia( SCalmWaterVs2Ps i, float2 vpos : VPOS ) : COLOR0
{  
#ifdef CALMWATER_NOSHADOWS
  return PSCalmWater( i, vpos );
#else
  half4 final;
  SCalmWaterPFT pft;
  float4 tcShadowMap = i.tc7.xyzw;
  half4 landShadow = 1; // probably something like selfshadow
  half diffShadowCoef = 0.5;
  //
  PFCalmWater0( i, pft );
  // tex coords for screen noise ( texture size is 64 )
  float2 tcsn = vpos.xy * ( 1.0f / 64 );// + PSC_CalmWaterPars2[15].xy;
  tcsn += pft.hMapA01.xy * 2.0; //
  half4 noise = tex2Dlod( sampler6, float4( tcsn.xy, 0, 0 ) ).rgba;
  pft.shadow = GetShadowSRDefaultNoise( tcShadowMap, vpos, landShadow, diffShadowCoef, noise );
  final = PFCalmWater1( i, pft, vpos );
  return final;
#endif
}

float4 PSCalmWaterTi(SCalmWaterVs2Ps i, float4 vpos : VPOS ) : COLOR0
{
  half temperature = AirTemperature()- 0.1f;
  float3 encodedTemp = EncodeTemperature(temperature);
  return half4(encodedTemp.rgb, 1.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSSRCalmWater_nVidia( SCalmWaterVs2Ps i, float4 vpos : VPOS ) : COLOR0
//float4 PSSRCalmWater_Default( SCalmWaterVs2Ps i, float2 vpos : VPOS ) : COLOR0
{
#ifdef CALMWATER_NOSHADOWS
  return PSCalmWater( i, vpos );
#else
  half4 final;
  SCalmWaterPFT pft;
  float4 tcShadowMap = i.tc7.xyzw;
  half4 landShadow = 1; // probably something like selfshadow
  half diffShadowCoef = 0.5;
  PFCalmWater0( i, pft );
  pft.shadow = GetShadowSRnVidia( tcShadowMap, vpos, landShadow, diffShadowCoef );
  final = PFCalmWater1( i, pft, vpos );
  final.rgb = pft.shadow * 0.5;
  return final;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
float4 PSSSSMCalmWater( SCalmWaterVs2Ps i, float4 vpos : VPOS ) : COLOR0
{ 
#ifdef CALMWATER_NOSHADOWS
  return PSCalmWater( i, vpos );
#else
  half4 final;
  SCalmWaterPFT pft;
  float4 tcShadowMap = i.tc7.xyzw;
  half4 landShadow = 1; // probably something like selfshadow
  half diffShadowCoef = 0.5;
  PFCalmWater0( i, pft );
  // tex coords for screen noise ( texture size is 64 )
  float2 tcsn = vpos.xy * ( 1.0f / 64 ) + PSC_CalmWaterPars2[15].xy;
  tcsn += pft.hMapA01.xy * 0.5; //
  half4 noise = tex2Dlod( sampler6, float4( tcsn.xy, 0, 0 ) ).rgba;
  pft.shadow = GetShadowBasicSSSMNoise( tcShadowMap, vpos, landShadow, diffShadowCoef, noise );
  final = PFCalmWater1( i, pft, vpos );
  return final;
#endif
}


//============================================================================================
//============================================================================================
// SIMUL WEATHER
//============================================================================================
//============================================================================================

// pixel shader constants
#define hazeEccentricity  PSC_SimulWeatherPars[0].x
#define interp  PSC_SimulWeatherPars[0].y
#define cloudEccentricity  PSC_SimulWeatherPars[0].z
#define alphaSharpness  PSC_SimulWeatherPars[0].w
#define mieRayleighRatio  PSC_SimulWeatherPars[1].xyz
#define fractalScale  PSC_SimulWeatherPars[2]
#define lightResponse  PSC_SimulWeatherPars[3]
#define lightningMultipliers  PSC_SimulWeatherPars[4]
#define lightningColour  PSC_SimulWeatherPars[5]
#define skylightColour  PSC_SimulWeatherPars[6].xyz
#define lightDir  PSC_SimulWeatherPars[7].xyz
#define fadeInterp PSC_SimulWeatherPars[7].w

// samplers
#define cloud_density_1 samplers3D[0]
#define cloud_density_2 samplers3D[1]
#define noise_texture samplers[2]
#define lightning_illumination samplers3D[3]
#define sky_loss_texture_1 samplers3D[4]
#define sky_loss_texture_2 samplers3D[5]
#define sky_inscatter_texture_1 samplers3D[6]
#define sky_inscatter_texture_2 samplers3D[7]

#define pi (3.1415926536f)

float HenyeyGreenstein(float g,float cos0)
{
  float g2=g*g;
  return 0.5*0.079577+0.5*(1.f-g2)/(4.f*pi*pow(1.f+g2-2.f*g*cos0,1.5f));
}

float3 InscatterFunction(float4 inscatter_factor,float cos0)
{
  float BetaRayleigh=0.0596831f*(1.f+cos0*cos0);
  float BetaMie=HenyeyGreenstein(hazeEccentricity,cos0);		// Mie's phase function
  float3 BetaTotal=(BetaRayleigh+BetaMie*inscatter_factor.a*mieRayleighRatio.xyz)
    /(float3(1.f,1.f,1.f)+inscatter_factor.a*mieRayleighRatio.xyz);
  float3 colour=BetaTotal*inscatter_factor.rgb;
  return colour;
}

#if FADE_MODE==1
float4 PSSimulWeatherCloudsWithLightning(SWCloudsVertexOutput IN): color
#else
float4 PSSimulWeatherCloudsWithLightningCPU(SWCloudsVertexOutput IN): color
#endif
{
  float3 noise_offset=float3(0.49803921568627452,0.49803921568627452,0.49803921568627452);

  // DX11 - convert bgra to rgba
  float3 noiseval=tex2D(noise_texture,IN.texCoordsNoise.xy).bgr-noise_offset;
#ifdef DETAIL_NOISE
  noiseval+=(tex2D(noise_texture,IN.texCoordsNoise.xy*8).bgr-noise_offset)/2.0;
  noiseval*=IN.texCoords.w;
#endif
  float3 pos=IN.texCoords.xyz+fractalScale.xyz*noiseval;

  // DX11 - convert bgra to rgba
  float4 density=tex3D(cloud_density_1,pos).bgra;
  float4 density2=tex3D(cloud_density_2,pos).bgra;

  density=lerp(density,density2,interp);

  density.x*=IN.layerFade_Fog.x;
  density.x=saturate(density.x*(1.f+alphaSharpness)-alphaSharpness);

  if(density.x<=0&&density.z<=0)
    discard;

  float3 view=normalize(IN.wPosition);
  float cos0=dot(lightDir.xyz,view.xyz);
  float Beta=lightResponse.x*HenyeyGreenstein(cloudEccentricity*density.z,cos0);
#if FADE_MODE==1
  float3 loss1=tex3D(sky_loss_texture_1,IN.fade_texc).rgb;
  float3 loss2=tex3D(sky_loss_texture_2,IN.fade_texc).rgb;
  float3 loss=lerp(loss1,loss2,fadeInterp);
  float4 insc1=tex3D(sky_inscatter_texture_1,IN.fade_texc);
  float4 insc2=tex3D(sky_inscatter_texture_2,IN.fade_texc);
  float4 insc=lerp(insc1,insc2,fadeInterp);
  float3 inscatter=InscatterFunction(insc,cos0);
#endif
#if FADE_MODE==0
  float3 loss=IN.loss;
  float3 inscatter=IN.inscatter;
#endif
  float4 lightning=tex3D(lightning_illumination,IN.texCoordLightning.xyz);

  float3 ambient=density.w*skylightColour.rgb;
  float opacity=density.x;
  float l=dot(lightningMultipliers.xyzw,lightning.xyzw);
  float3 lightningC=l*lightningColour.xyz;

  float3 final=(density.z*Beta+lightResponse.y*density.y)*IN.lightColour+ambient.rgb;
  final+=lightningColour.w*lightningC;

  final*=loss;
  final+=inscatter;

  // multiply by Arma sky color - this ensures modification by eye accom
  final.rgb *= PSC_AE.rgb;

  final+=lightningC*(opacity+IN.layerFade_Fog.x);

  // fog application
  final.rgb = lerp(final.rgb, PSC_FogColor.rgb, IN.layerFade_Fog.y);

  final*=opacity;

  return float4(final,opacity);
}

#if FADE_MODE==1
float4 PSSimulWeatherClouds(SWCloudsVertexOutput IN): color
#else
float4 PSSimulWeatherCloudsCPU(SWCloudsVertexOutput IN): color
#endif
{
  float3 noise_offset=float3(0.49803921568627452,0.49803921568627452,0.49803921568627452);

  // DX11 - convert bgra to rgba
  float3 noiseval=tex2D(noise_texture,IN.texCoordsNoise.xy).bgr-noise_offset;
#ifdef DETAIL_NOISE
  noiseval+=(tex2D(noise_texture,IN.texCoordsNoise.xy*8).bgr-noise_offset)/2.0;
  noiseval*=IN.texCoords.w;
#endif
  float3 texcoord=IN.texCoords.xyz+fractalScale.xyz*noiseval;

  // DX11 - convert bgra to rgba
  float4 density=tex3D(cloud_density_1,texcoord).bgra;
  float4 density2=tex3D(cloud_density_2,texcoord).bgra;

  density=lerp(density,density2,interp);

  density.x*=IN.layerFade_Fog.x;
  density.x=saturate(density.x*(1.f+alphaSharpness)-alphaSharpness);

  if(density.x<=0&&density.z<=0)
    discard;
  float3 view=normalize(IN.wPosition);
  float cos0=dot(lightDir.xyz,view.xyz);
  // cloudEccentricity is multiplied by density.z (i.e. direct light) to avoid interpolation artifacts.
  float Beta=lightResponse.x*HenyeyGreenstein(cloudEccentricity*density.z,cos0);
  // Fade mode 1 means using textures for distance fade.
#if FADE_MODE==1
  float4 insc1=tex3D(sky_inscatter_texture_1,IN.fade_texc);
  float4 insc2=tex3D(sky_inscatter_texture_2,IN.fade_texc);
  float4 insc=lerp(insc1,insc2,fadeInterp);
  float3 loss1=tex3D(sky_loss_texture_1,IN.fade_texc).rgb;
  float3 loss2=tex3D(sky_loss_texture_2,IN.fade_texc).rgb;
  float3 loss=lerp(loss1,loss2,fadeInterp);
  float3 inscatter=InscatterFunction(insc,cos0);
#endif
  // Fade mode 0 means passing fade values in through vertex properties.
#if FADE_MODE==0
  float3 loss=IN.loss;
  float3 inscatter=IN.inscatter;
#endif
  float3 ambient=skylightColour.rgb*density.w;

  float opacity=density.x;
  float3 final=(density.z*Beta+lightResponse.y*density.y)*IN.lightColour+ambient.rgb;

  final*=loss;
  final+=inscatter;

  // multiply by Arma sky color - this ensures modification by eye accom
  final.rgb *= PSC_AE.rgb; 

  // fog application
  final.rgb = lerp(final.rgb, PSC_FogColor.rgb, IN.layerFade_Fog.y);

  final*=opacity;

  return float4(final,opacity);
}

//============================================================================================

//============================================================================================
//============================================================================================
// Caustics
//============================================================================================
//============================================================================================
#define CAUSTICS_SAMPLER sampler14


// length of side of rectangle, that caustics texture covers (in zero depth)
#define CAUSTICS_TEXTURE_AREA PSC_CausticsPars[0].x 
// length of side of rectangle, that caustics texture covers (in max depth)
#define CAUSTICS_TEXTURE_AREA_DEEP PSC_CausticsPars[0].y 
// coef for depth fading
#define CAUSTICS_TEXTURE_AREA_DEPTH_FADE_COEF PSC_CausticsPars[0].z 
// time
#define CAUSTICS_TIME PSC_CausticsPars[0].w

// distance limit in m
#define CAUSTICS_DISTANCE_LIMIT PSC_CausticsPars[1].x
// depth limit in m
#define CAUSTICS_DEPTH_LIMIT PSC_CausticsPars[1].y
// water level in m
#define CAUSTICS_WATER_LEVEL PSC_CausticsPars[1].z
// brightness coefficient
#define CAUSTICS_BRIGHTNESS_COEF PSC_CausticsPars[1].w

// 1.0f/numRow, where numRows is number of animated textures in one row of caustics texture
// we assume that number of rows and columns is the same
#define CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL PSC_CausticsPars[2].x 
// which animated texture is chosen in x-coord (in column of texture)
#define CAUSTICS_TEXTURE_X PSC_CausticsPars[2].y 
// which animated texture is chosen in y-coord (in row of texture)
#define CAUSTICS_TEXTURE_Y PSC_CausticsPars[2].z 
/// distance (in meters) over which we interpolate two caustics textures (in different depth)
#define CAUSTICS_TEXTURE_DEPTH_GRANULARITY PSC_CausticsPars[2].w

// coef used to multply texture coordinates to get the value, that breaks repetition of caustics
#define CAUSTICS_BREAK_TEXTURE_COEF 0.1f
// in which depth under water caustics are in full force
#define CAUSTICS_UNDERWATER_LIMIT 0.2f
// if we are using break coords (disabled now, breaking does not work well with depth varied coords)
#define CAUSTICS_USE_BREAK_TEX 0

float4 PSCaustics(
                  float4 vPos : SV_POSITION,
                  float4 worldPos_LDot: TEXCOORD6, 
                  float4 transformedPos_Fog: TEXCOORD7,
                  float3 causticTexCoord_depthStart: TEXCOORD5): COLOR0
{
  float fog = transformedPos_Fog.w;
  clip(fog-0.0001); //fogged -> not visible

  /// depth under water
  float depth = CAUSTICS_WATER_LEVEL - worldPos_LDot.y;

  /// where the caustics should start (in m)
  float depthStart = causticTexCoord_depthStart.z;

  /// not under water - no caustics
  clip(depth - depthStart);

  /// too deep
  clip(CAUSTICS_DEPTH_LIMIT - depth);

  /// distance from camera  
  float dist = length(transformedPos_Fog.xyz);
  /// too far
  clip(CAUSTICS_DISTANCE_LIMIT - dist);

  /// depth for the first caustics tex coordinates
  float depth1 = floor(depth / CAUSTICS_TEXTURE_DEPTH_GRANULARITY) * CAUSTICS_TEXTURE_DEPTH_GRANULARITY;
  /// depth for the second caustics tex coordinates
  float depth2 = depth1 + CAUSTICS_TEXTURE_DEPTH_GRANULARITY;
  /// interpolation coef for two textures in two depths
  float depthLerpCoef = saturate((depth - depth1) / CAUSTICS_TEXTURE_DEPTH_GRANULARITY);

  /// coef used to multiply texture coords in first depth
  float depthTexCoef1 = lerp(
    CAUSTICS_TEXTURE_AREA, 
    CAUSTICS_TEXTURE_AREA_DEEP, 
    1.0f-saturate((CAUSTICS_DEPTH_LIMIT - depth1)/CAUSTICS_DEPTH_LIMIT)    
    );

  /// coef used to multiply texture coords in second depth
  float depthTexCoef2 = lerp(
    CAUSTICS_TEXTURE_AREA, 
    CAUSTICS_TEXTURE_AREA_DEEP,
    1.0f - saturate((CAUSTICS_DEPTH_LIMIT - depth2)/CAUSTICS_DEPTH_LIMIT)
    );

  /// tex coords in first depth
  float2 coords1 = causticTexCoord_depthStart.xy / depthTexCoef1;
  /// tex coords in second depth
  float2 coords2 = causticTexCoord_depthStart.xy / depthTexCoef2;

  // add some movement based on time
  coords1.x += sin(CAUSTICS_TIME + coords1.x)*0.01f;
  coords1.y += cos(CAUSTICS_TIME + coords1.y)*0.01f;

  coords2.x += sin(CAUSTICS_TIME + coords2.x)*0.01f;
  coords2.y += cos(CAUSTICS_TIME + coords2.y)*0.01f;

#if CAUSTICS_USE_BREAK_TEX
  // coords used to break repetition of caustics
  float2 breakCoords = coords2 * CAUSTICS_BREAK_TEXTURE_COEF;
#endif

  // compute coords in animated texture
  coords1.x = (CAUSTICS_TEXTURE_X + frac(coords1.x)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;
  coords1.y = (CAUSTICS_TEXTURE_Y + frac(coords1.y)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;

  coords2.x = (CAUSTICS_TEXTURE_X + frac(coords2.x)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;
  coords2.y = (CAUSTICS_TEXTURE_Y + frac(coords2.y)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;

#if CAUSTICS_USE_BREAK_TEX
  breakCoords.x = (CAUSTICS_TEXTURE_X + frac(breakCoords.x)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;
  breakCoords.y = (CAUSTICS_TEXTURE_Y + frac(breakCoords.y)) * CAUSTICS_TEXTURE_NUM_ROWS_RECIPROCAL;
#endif

  // sample colors in both depths
  // we have to use tex2Dlod, otherwise artifacts may appear (news:is5qgc$onn$1@new-server.localdomain)
  float4 color1 = tex2Dlod(CAUSTICS_SAMPLER, float4(coords1.xy, 0, 0));
  float4 color2 = tex2Dlod(CAUSTICS_SAMPLER, float4(coords2.xy, 0, 0));

  // compute lerped color
  float4 color = lerp(color1, color2, depthLerpCoef);

  // shadow coef
  float shadowCoef = tex2Dlod(samplerShadowMap, float4((vPos.xy + float2(0.5, 0.5))*PSC_InvW_InvH_X_X.xy, 0, 1)).r;
  color.a *= shadowCoef;

  // diffuse coef (we want the caustics to have some effect on the back side as well)
  color.a *= max(worldPos_LDot.w, 0);

  // shore coef
  color.a *= saturate((CAUSTICS_WATER_LEVEL - worldPos_LDot.y - depthStart)  / CAUSTICS_UNDERWATER_LIMIT);

  // distance coef
  float distCoef = 1.0f - saturate((CAUSTICS_DISTANCE_LIMIT - dist)/CAUSTICS_DISTANCE_LIMIT);
  color.a *= 1.0f - distCoef*distCoef*distCoef;

  // fog coef
  color.a *= fog;

  // depth coef
  float depthCoef = saturate((CAUSTICS_DEPTH_LIMIT - depth)/CAUSTICS_DEPTH_LIMIT);
  depthCoef = pow(depthCoef, CAUSTICS_TEXTURE_AREA_DEPTH_FADE_COEF);
  color.a *= depthCoef;

#if CAUSTICS_USE_BREAK_TEX
  color.a *= 1.0f - tex2Dlod(CAUSTICS_SAMPLER, float4(breakCoords.xy, 0, 0)).a;
#endif 

  // final color
  color *= PSC_Diffuse;

  // multiply by brightness coef
  color.a *= CAUSTICS_BRIGHTNESS_COEF;

  return float4(color.rgb * color.a, color.a);
}

//============================================================================================
//============================================================================================
//============================================================================================
// Refraction shader - _ARMA3_REFRACTION
//============================================================================================
//============================================================================================

#define REFRACT_DIFFUSE_SAMPLER sampler0
#define REFRACT_NORMALMAP_SAMPLER sampler1
#define REFRACT_SPECULARMAP_SAMPLER sampler2
#define REFRACT_FRESNEL_SAMPLER sampler3
#define REFRACT_ENVMAP_SAMPLER sampler4
/// background texture
#define REFRACT_BG_SAMPLER sampler10
/// depth texture
#define REFRACT_DEPTH_SAMPLER sampler11

#define REFRACT_SCREEN_SCALE  PSC_RefractPars[0].xy
#define REFRACT_SCREEN_OFFSET PSC_RefractPars[0].zw
#define REFRACT_INTENSITY PSC_RefractPars[1].x
#define REFRACT_SCALE PSC_RefractPars[1].yzw
#define REFRACT_TIME_HORIZONTAL PSC_RefractPars[2].x
#define REFRACT_TIME_VERTICAL PSC_RefractPars[2].y
#define REFRACT_USE_ABERRATION PSC_RefractPars[2].z

/// chromatic aberation
#define REFRACT_CHROM_ABER 1
/// specular
#define REFRACT_SPECULAR 1

// returns NON-NORMALIZED normal decoded from DXT5
half3 GetHQNormal(sampler2D s, half2 coords)
{
  half4 normal4 = tex2D(s, coords);
  half3 dxt5FixedNormal = half3(1 - normal4.a, 0, 0) + normal4.rgb;
  return (dxt5FixedNormal * 2 - 1);
}

FOut PFRefract(SRefract input, bool sbShadows, bool thermal)
{
  /// Get the LWS position
  float3 lwsPosition = float3(input.stn2lws0.w, input.stn2lws1.w, input.stn2lws2.w);

  /// Normal non-modified by normal map (in LWS)
  half3 lwsNormalNonMod = float3(input.stn2lws0.z,input.stn2lws1.z,input.stn2lws2.z);

  /// Get normal from normal map
  /// We use two normals (for animations) and average them

  half2 normalCoords = input.tDiffuseMap_NormalMap.wz + half2(sin(REFRACT_TIME_HORIZONTAL), 0);
  half3 normal1 = GetHQNormal(REFRACT_NORMALMAP_SAMPLER, normalCoords);

  normalCoords = input.tDiffuseMap_NormalMap.wz + half2(0, sin(REFRACT_TIME_VERTICAL));
  half3 normal2 = GetHQNormal(REFRACT_NORMALMAP_SAMPLER, normalCoords);

  half3 normal = (normal1 + normal2) * 0.5f;

  // Get the normal in LWS space
  half3 lwsNormal;
  // Transform normal to LWS
  lwsNormal.x = dot(input.stn2lws0.xyz, normal);
  lwsNormal.y = dot(input.stn2lws1.xyz, normal);
  lwsNormal.z = dot(input.stn2lws2.xyz, normal);

  // Normalize the result, because the stn2lws may be inaccurate due to per vertex artefacts
  lwsNormal = normalize(lwsNormal);

  // Calculate vector to eye
  float3 lwsEye = normalize(-lwsPosition.xyz);

  // compute intensity coef
  // we want to decrease the intensity based on non-modified normals (so that the anomaly effect has no sharp boundary)
  half intensityCoef = saturate( (dot(lwsEye, lwsNormalNonMod) - 0.2f) / 0.8f);
  intensityCoef *= REFRACT_INTENSITY;

  // texcoord in background texture
  half2 texcoord = input.vPos * REFRACT_SCREEN_SCALE + REFRACT_SCREEN_OFFSET; 

  // compute color
  half3 color;

  // z depth of this pixel
  float zdepth = input.projectedPos.z; 

#if REFRACT_CHROM_ABER
  // chromatic aberation
  if (REFRACT_USE_ABERRATION > 0.0f)
  {
    [unroll]
    for (int i=0; i<3; ++i)
    {
      half2 move = intensityCoef * REFRACT_SCALE[i] * normal.xy;
      half2 coords = texcoord + move;

      float sampleddepth   = tex2D(REFRACT_DEPTH_SAMPLER, coords).x;

      //get depth in depth buffer and of water
      float pixeld = 1.0/(sampleddepth + saturate(0.001 - sampleddepth));
      float diff  = pixeld - zdepth;

      //if diff2 < 0 -> new pixel is above water, do not take it, otherwise the pixels above water will move, too
      float depthCoef = saturate(diff*1000);
      color[i] = tex2D(REFRACT_BG_SAMPLER, texcoord + move * depthCoef)[i];
    }
  }
  else
#endif
  {
    half2 move = intensityCoef * REFRACT_SCALE.r * normal.xy;
    half2 coords = texcoord + move;

    float sampleddepth   = tex2D(REFRACT_DEPTH_SAMPLER, coords).x;

    //get depth in depth buffer and of water
    float pixeld = 1.0/(sampleddepth + saturate(0.001 - sampleddepth));
    float diff  = pixeld - zdepth;

    //if diff2 < 0 -> new pixel is further, do not take it
    float depthCoef = saturate(diff*1000);
    color = tex2D(REFRACT_BG_SAMPLER, texcoord + move * depthCoef).rgb;
  }

  // diffuse texture color modification
  half4 diffuseColor = tex2D(REFRACT_DIFFUSE_SAMPLER, input.tDiffuseMap_NormalMap.xy);
  half3 colorMod = lerp(half3(1,1,1), diffuseColor.rgb, intensityCoef);
  color = colorMod * color;

  FOut fout = InitFOut();
  fout.color = half4(color, 1);
  fout.light.specular = half3(0,0,0);
  fout.light.specularEnv  = half3(0,0,0);
  fout.light.indirect = half3(1,1,1);
  fout.light.direct = half3(0,0,0);
  fout.light.diffuseShadowCoef = 1;
  fout.fog = 1;

#if REFRACT_SPECULAR
  // Calculate the O vector (the reflection vector)
  float3 o = lwsNormal.xyz * dot(lwsNormal.xyz, lwsEye) * 2.0f - lwsEye;

  // Calculate the halfway vector in LWS space
  float3 lwsHalfway = normalize(lwsEye + PSC_WLight);

  // Get the specular map content
  half4 specular = tex2D(REFRACT_SPECULARMAP_SAMPLER, input.tSpecularMap.xy);

  // Calculate the specular power and environmental map power coef
  // Inner Max is here to avoid zero division
  // Outer Max is here to not to cause aliasing in DDX and DDY (to not to make them smaller than 1)
  half specularPower = PSC_Specular.a * specular.b;
  half specularPowerEnvCoef = max(500/max(specularPower, 1), 1); // Heuristic conversion of specularPower to env. map. bluring coefficient

  // Get the color from the environmental map
  float2 envT = o.xy * float2(1, -1) * 0.5 + 0.5;
  half3 envColor = tex2Dgrad(REFRACT_ENVMAP_SAMPLER, envT, ddx(envT)*specularPowerEnvCoef, ddy(envT)*specularPowerEnvCoef) * PSC_GlassEnvColor.rgb;

  // Get the Fresnel reflection coefficient
  half fresnelCoef = tex2D(REFRACT_FRESNEL_SAMPLER, float2(dot(lwsNormal, o), 0.5)).a;

  // Get the specular coefficient
  half specularCoef = fresnelCoef * specular.g * intensityCoef;

  half nDotL = dot(lwsNormal, PSC_WLight);
  half4 litVector = lit(nDotL, dot(lwsNormal, lwsHalfway), PSC_Specular.a * specular.b);
  half coefSpecular = litVector.z;
  coefSpecular = min(coefSpecular, 1); // Specular power can be very high and this can probably lead to infinity (if the coefSpecular happen to be just slightly above 1)

  fout.light.specular = PSC_Specular * coefSpecular * specularCoef;
  fout.light.specularEnv = envColor * PSC_GlassMatSpecular * 2.0f * specularCoef; // 2 is here because specular color is halved
  fout.light.indirect = (1-specularCoef)*fout.light.indirect;
#endif // REFRACT_SPECULAR


  return fout;
}

//============================================================================================
//============================================================================================
//============================================================================================
// Refraction shader for sprites - _ARMA3_REFRACTION_SPRITES
//============================================================================================
//============================================================================================


#define S_REFRACT_DIFFUSE_SAMPLER sampler0
/// background texture
#define S_REFRACT_BG_SAMPLER sampler10
/// depth texture
#define S_REFRACT_DEPTH_SAMPLER sampler11

#define S_REFRACT_SCREEN_SCALE  PSC_RefractPars[0].xy
#define S_REFRACT_SCREEN_OFFSET PSC_RefractPars[0].zw
#define S_REFRACT_INTENSITY PSC_RefractPars[1].x
#define S_REFRACT_SCALE PSC_RefractPars[1].yzw
#define S_REFRACT_TIME_HORIZONTAL PSC_RefractPars[2].x
#define S_REFRACT_TIME_VERTICAL PSC_RefractPars[2].y
#define S_REFRACT_USE_ABERRATION PSC_RefractPars[2].z

/// chromatic aberation
#define S_REFRACT_CHROM_ABER 0

FOut PFSpriteRefractCommon(SSprite input, bool sbShadows, bool thermal, bool useDepth)
{
  if (thermal)
    discard; // no refraction particles when thermal imaging is on

  // first get alpha from non-modified tex coords
  half4 tex0 = tex2D(S_REFRACT_DIFFUSE_SAMPLER, input.tDiffuseMap.xy);

  // final alpha
  half finalAlpha = tex0.a *  input.ambientColor.a * input.modulateColor.a;

  clip(finalAlpha - 0.001); //no need to continue if alpha too low

  //// compute offset for refraction
  //// We use two shifted values (for animations) and average them
  //half2 moveCoords = input.tDiffuseMap.xy + half2(sin(S_REFRACT_TIME_HORIZONTAL), 0);
  //half3 offset1 = tex2D(S_REFRACT_DIFFUSE_SAMPLER, moveCoords);

  //moveCoords = input.tDiffuseMap.xy + half2(0, sin(S_REFRACT_TIME_VERTICAL));
  //half3 offset2 = tex2D(S_REFRACT_DIFFUSE_SAMPLER, moveCoords);

  //// final offset
  //half3 offset = (offset1 + offset2)*0.5f; //no need to normalize since we do not need the value to be exactly of size 1

  half3 offset = tex2D(S_REFRACT_DIFFUSE_SAMPLER, input.tDiffuseMap.xy)*2.0f - 1.0f ;

  // texture coords for BG and depth buffer
  half2 texcoord = input.vPos * S_REFRACT_SCREEN_SCALE + S_REFRACT_SCREEN_OFFSET; 
  //half2 texcoord = input.tPosition.xy+(PSC_InvW_InvH_X_X.xy*0.5);

  // coef based on depth information
  half depthCoef = 1.0f;
  if (useDepth)
  {
    // compute the depth coef
    // we do not want to refract pixels that are in front of us

    // if we want this to be correct, we should check depth for each RGB component separately, but for the simplicity sake we will use just R offset
    half2 move = finalAlpha * S_REFRACT_SCALE[0] * offset.xy;
    half2 coords = texcoord + move;

    // zdepth of this pixel
    half zdepth = input.tPosition.z - input.tPosition.w; //we want to get real z (without particle diameter)

    float sampleddepth   = tex2D(S_REFRACT_DEPTH_SAMPLER, coords).x;
    //get depth in depth buffer
    float pixeld = 1.0/(sampleddepth + saturate(0.001 - sampleddepth));
    float diff  = pixeld - zdepth;
    // if diff < 0, then the pixel in BG is in front of us - we do not want to refract it
    depthCoef = saturate(diff*1000);
  }

  half3 color;

  // get color value from BG texture
#if S_REFRACT_CHROM_ABER
  // chromatic aberation
  if (S_REFRACT_USE_ABERRATION > 0.0f)
  {
    color.r = tex2D(S_REFRACT_BG_SAMPLER, texcoord + offset.xy*finalAlpha*depthCoef*S_REFRACT_SCALE.r).r;
    color.g = tex2D(S_REFRACT_BG_SAMPLER, texcoord + offset.xy*finalAlpha*depthCoef*S_REFRACT_SCALE.g).g;
    color.b = tex2D(S_REFRACT_BG_SAMPLER, texcoord + offset.xy*finalAlpha*depthCoef*S_REFRACT_SCALE.b).b;
  }
  else
#endif
  {
    color = tex2D(S_REFRACT_BG_SAMPLER, texcoord + offset.xy*finalAlpha*depthCoef*S_REFRACT_SCALE.r).rgb;
  }

  // fill output structure
  FOut fout = InitFOut();

  fout.color = half4(color, finalAlpha);
  fout.light.specular = half3(0,0,0);
  fout.light.specularEnv  = half3(0,0,0);
  fout.light.indirect = half3(1,1,1);
  fout.light.direct = half3(0,0,0);
  fout.light.diffuseShadowCoef = 1;
  fout.fog = 1;

  return fout;
}

/// version with depth buffer available
FOut PFSpriteRefract(SSprite input, bool sbShadows, bool thermal)
{
  return PFSpriteRefractCommon(input, sbShadows, thermal, true);
}

/// version with no depth buffer available
FOut PFSpriteRefractSimple(SSprite input, bool sbShadows, bool thermal)
{
  return PFSpriteRefractCommon(input, sbShadows, thermal, false);
}


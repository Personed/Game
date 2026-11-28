#include "VS.h"


#if PRGDEFRenderingMode <= RMCommon
  #define COMMON 1
#else
  #define COMMON 0
#endif

void VSTerrain(
  in float4 vPosition             : POSITION0,
  in uint3 iNormal               : NORMAL,
  in int2 iTexCoord0            : TEXCOORD0,
  in uint3 iS                    : TANGENT0,
  in uint3 iT                    : TANGENT1,
  in float3 lod                   : POSITION1,
#if _VBS3_MORETERRAINLODS_SHADER
  in float4 lod36                     : POSITION2,
#endif
#if _VBS3_CPU_LAND_LIGHT_SHADER
  in uint4 iStaticLight           : TEXCOORD1,
#endif
#if _VBS3_LAND_SHADOW_SHADER
  in float1 landShadow            : TEXCOORD2,
#endif
  ISOLATE out float4 oProjectedPosition   : POSITION,
#if COMMON
  out float4 oOutAmbient          : TEXCOORD_AMBIENT,
  out float4 oOutSpecular         : TEXCOORD_SPECULAR,
  out float4 oSatAndMask          : TEXCOORD4,
  out float4 oShadowMap           : TEXCOORDSR,
  out float3 oLightLocal          : COLOR0,
  out float4 oOutSkinnedPos_Fog   : COLOR1,
#elif PRGDEFRenderingMode == RMShadowBuffer
  out float4 oPosition            : TEXCOORD7,
#elif PRGDEFRenderingMode == RMZPrime || PRGDEFRenderingMode == RMDPrime
  out float4 oPosition            : TEXCOORD7,
#elif PRGDEFRenderingMode == RMCrater
  out float4 oOutSkinnedPos_Fog   : COLOR1,
  out float4 oPosition            : TEXCOORD7,
#elif PRGDEFRenderingMode == RMCaustics
  out CausticsOut causticsOut,
#endif
  out float4 oTexCoord[4]         : TEXCOORD0)
{
  // DX11 - convert uints to floats
  float3 vNormal = BYTETOFLOAT01(iNormal.xyz);
  float3 vS = BYTETOFLOAT01(iS.xyz);
  float3 vT = BYTETOFLOAT01(iT.xyz);

  float3 vTexCoord0 = float3(iTexCoord0.xy, 1);

  // Decompress compressed vectors
  DecompressVector(vS);
  DecompressVector(vT);
  DecompressVector(vNormal);

  // grass alpha not needed
  float2 grassAlphaOut;

#if _VBS3_MORETERRAINLODS_SHADER
  TransformOutput to = VTransformLodEx(vPosition, vNormal, lod, lod36, grassAlphaOut, false);
#else
  TransformOutput to = VTransformLodEx(vPosition, vNormal, lod, grassAlphaOut, false);
#endif

  oProjectedPosition = to.projectedPosition;

  // Zero the output texcoords to make sure all are set (most of it probably will be removed by optimizer)
  for (int i = 0; i < 4; i++) oTexCoord[i] = float4(0, 0, 0, 0);

  #if COMMON
  {
    // Initialize ligths
    AccomLights al;
    VertexInitLights(to.instanceColor.a,to.instanceShadow, al);

   #if _VBS3_CPU_LAND_LIGHT_SHADER
     float4 staticLight = BYTETOFLOAT01_BGRA2RGBA(iStaticLight);
     float4 decodedStaticLight = float4((staticLight.rgb * VSC_TerrainStaticModulate_X.xyz) * staticLight.a * 255, 0);
     al.ambient += decodedStaticLight;
   #endif

    // Terrain
    float3 bedColor;
    {
      // Calculate land shadow
#if _VBS3_LAND_SHADOW_SHADER
      al.specular.w = landShadow;
#else
      {
        // Calculate coordinate in grid
        float2 coordinate = to.skinnedPosition.xz * VSC_LAND_SHADOWGRID_GRID__1__GRIDd2.xx + VSC_LAND_SHADOWGRID_GRID__1__GRIDd2.ww;
        float4 weights;
        weights.xy = frac(coordinate);

        // Remove the fractional part from y - this needs to be done for y, as it is multiplied
        coordinate.xy -= weights.xy;

        // Calculate the index to VSC_LAND_SHADOW array
        float2 satXY = max(0, min(coordinate.xy, VSC_LAND_SHADOWGRID_GRID__1__GRIDd2.y - 1.0f));
        float shadowFragmentIndex = satXY.y * VSC_LAND_SHADOWGRID_GRID__1__GRIDd2.y + satXY.x;

        // weights will contain: xFrac, zFrac, 1-xFrac, 1-zFrac
        weights.zw = 1 - weights.xy;

        // (1-xFrac)*(1-zFrac), xFrac*(1-zFrac), (1-xFrac)*zFrac, xFrac*zFrac
        weights = weights.zxzx * weights.wwyy;

        // Bilinear interpolation between the four corners
        al.specular.w = dot(VSC_LAND_SHADOW[shadowFragmentIndex], weights);
        // darken vertices with negative Y
        // such vertices are under the sea
        // we do this partly here, partly in DoneLightsTerrain
        //vSpecular.w = max(min(-skinnedPosition.y*(1.0/10),1),vSpecular.w);
      }
#endif

      // LUKE - FIX (strange black faces on sea bed)
      float shallow = 1; //saturate(1+to.skinnedPosition.y*(1.0/10));

      // compute sed bed color change - 1 for deep water, 0 for shallow water
      float deep = saturate(-to.skinnedPosition.y*(1.0/15));
      bedColor = lerp(1,float3(0,0.05,0.15),deep);

      // Light and Halfway calculation
      float3 lightLocal;
      float3 halfwayLocal;
      float3 eyeLocal;
      float3 halfwayReflectLocal;
      float3 upLocal;
      CalculateLocalLightAndHalfway(vS, vT, to.skinnedPosition, to.skinnedNormal, to.skinnedMatrix0, to.skinnedMatrix1, to.skinnedMatrix2, lightLocal, halfwayLocal, eyeLocal, halfwayReflectLocal, upLocal);

      // Calculate the local eye vector  
      float3 camDirection = VSC_CameraPosition.xyz - to.skinnedPosition;
      eyeLocal.xyz = camDirection.xzy;

      
      // Calculate the ligth local
      // by modifying the light vector we affect lighting results
      oLightLocal.xyz = lightLocal*shallow;
      
      // Set the eye vector - used for specular or parallax
      oTexCoord[3].xyz = eyeLocal;
      
      // Calculate world texcoord for adressing the sattelite map and the layer mask
      vPosition.w = 1;
      oSatAndMask.x = dot(vPosition, VSC_TexTransform[4]._m00_m10_m20_m30);
      oSatAndMask.y = dot(vPosition, VSC_TexTransform[4]._m01_m11_m21_m31);
      oSatAndMask.w = dot(vPosition, VSC_TexTransform[1]._m00_m10_m20_m30);
      oSatAndMask.z = dot(vPosition, VSC_TexTransform[1]._m01_m11_m21_m31);
    }

    // Apply fog
    float oFog;
    VFogFogAlpha(to, al, oFog);

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

#if 1 // _VBS3_PERPIXELPSLIGHTS
    // Write lights to output
    VDoneLightsTerrain(al, bedColor, oOutAmbient, oOutSpecular);
    oOutSkinnedPos_Fog = float4(to.skinnedPosition.xyz, oFog);

    // Pass the normal into pixel shader
    oTexCoord[2].xyz = to.skinnedNormal.xyz;

    // Texture coordinates transformation
    TexCoordTransform(vTexCoord0, vTexCoord0, 4, oTexCoord);
#else
    // Write lights to output
    VDoneLightsTerrain(al, bedColor, oOutAmbient, oOutSpecular);
    oOutSkinnedPos_Fog = float4(0, 0, 0, oFog);

    // Texture coordinates transformation
    TexCoordTransform(vTexCoord0, vTexCoord0, 5, oTexCoord);
#endif
  }
  #else
  {
    TexCoordTransform(vTexCoord0, vTexCoord0, 1, oTexCoord);
    #if PRGDEFRenderingMode == RMShadowBuffer
    {
      if (VSC_SBTechniqueDefault)
      {
        oPosition = to.projectedPosition;
      }
      else
      {
        oPosition = 0;
      }
    }
    #elif PRGDEFRenderingMode == RMZPrime || PRGDEFRenderingMode == RMDPrime
    {
      oPosition = to.transformedPosition;
    }
    #elif PRGDEFRenderingMode == RMCrater
    {
      float oFog;
      VFog(to, oFog);
#if 1 // _VBS3_PERPIXELPSLIGHTS
      oOutSkinnedPos_Fog = float4(to.skinnedPosition.xyz, oFog);
      oTexCoord[2].xyz = to.skinnedNormal.xyz;
#else
      oOutSkinnedPos_Fog = float4(0, 0, 0, oFog);
#endif
      oPosition = to.position;
    }
#elif PRGDEFRenderingMode == RMCaustics
    {
      FillCausticsOut(to, causticsOut);
    }
#endif
  }
  #endif
}
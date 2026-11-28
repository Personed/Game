#include "VS.h"


//------------------------------------------------------------------------------------
/*
get grid position
*/
//------------------------------------------------------------------------------------
float4 ComputeGridPos(float2 worldpos)
{
    float2 gridPos = worldpos * VSC_WaveGrid.xx + VSC_WaveGrid.ww;

    gridPos = max(0,min(gridPos,VSC_WaveGrid.z));
    //float2 gridPos = vTex0.xy*VSC_WaveGrid.xx + VSC_WaveGrid.ww;
    float2 gridFrac = frac(gridPos);

    // remove the fractional part from y - this needs to be done for y, as it is multiplied
    gridPos -= gridFrac;

    float4 gridPosFrac = float4(gridFrac.x,gridFrac.y,1-gridFrac.x,1-gridFrac.y);

    // (1-xFrac)*(1-zFrac), xFrac*(1-zFrac), (1-xFrac)*zFrac, xFrac*zFrac
    float4 gridF = gridPosFrac.zxzx*gridPosFrac.wwyy;
    
    // xy contains: xFrac, zFrac
    // zw contains: 1-xFrac, 1-zFrac   
    float gridAddr0 = VSC_WaveGrid.y*gridPos.y + gridPos.x;
    float gridAddr1 = gridAddr0+VSC_WaveGrid.y;
    
    float4 gridSample;
    gridSample = gridF.x*VSC_WaterDepth[gridAddr0]; // y00,y10 (yXZ)
    gridSample += gridF.y*VSC_WaterDepth[gridAddr0+1]; // y00,y10 (yXZ)
    gridSample += gridF.z*VSC_WaterDepth[gridAddr1]; // y01,y11
    gridSample += gridF.w*VSC_WaterDepth[gridAddr1+1]; // y01,y11
	 
	 return gridSample;
}



//------------------------------------------------------------------------------------
/*
vertex movement by sin and cos computation
*/
//------------------------------------------------------------------------------------
float2 ComputeWaterMovement(float3 t0, out float waveHeight, out float topDetect, out float wavemapx)
{    
    #define H_PI 3.1415926536
    const float4 PI_0p5_PI2_0p25={
      H_PI,0.5,H_PI*2,0.25
    };
    const float4 SIN_TAYLOR_3579={
      -1.0/(3*2),1.0/(5*4*3*2),-1.0/(7*6*5*4*3*2),+1.0/(9*8*7*6*5*4*3*2)
    };
    const float4 COS_TAYLOR_2468={
      -1.0/2,+1.0/(4*3*2),-1.0/(6*5*4*3*2),+1.0/(8*7*6*5*4*3*2)
    };

    float2 tex0;
    tex0.x = dot(t0,VSC_TexTransform[0]._m00_m10_m30);
    tex0.y = dot(t0,VSC_TexTransform[0]._m01_m11_m31);
   
    // calculate phase offset for sin. argument - P(y)
    // valWAVEGEN
    const float4 waveGen=
    {
      // wave phase offset  - due to looping, when multiplied by 8, must give integer
      2.0/8,3.0/8,
      // wave offset amplitude - arbitrary
      0.2,0.12
    };
	 
    // first version - P(y) = sin(y)
    float2 waveTime = tex0.yy * waveGen.xy;

    // x = x0*2*pi-pi
    
    float2 x = frac(waveTime)*PI_0p5_PI2_0p25.zz - PI_0p5_PI2_0p25.xx;

    // calculate 2x sin at once, using Taylor series
    float4 x24 = x.xyxy*x.xyxy; // r1.xy = r0^2.xy, r1.zw = r0^2.xy
    x24.zw = x24.xyxy * x24.xyxy; // r1.xy = r0^2.xy, r1.zw = r0^4.xy

    float4 x35 = x.xyxy * x24.xyzw; // r2.xy = r0^3.xy, r2.zw = r0^5.xy
    float4 x79 = x35.xyzw * x24.zwzw; // r3.xy = r0^7.xy, r2.zw = r0^9.xy

    float2 sinX = (
      x +
      x35.xy*SIN_TAYLOR_3579.x + 
      x35.zw*SIN_TAYLOR_3579.y +
      x79.xy*SIN_TAYLOR_3579.z +
      x79.zw*SIN_TAYLOR_3579.w
    );
    tex0.x += sinX.x*waveGen.z+sinX.y*waveGen.w;
    
    // wave top is where sin(x)==1 -> x = pi/2 -> x0 = 3/4;
    // white line is on u = 0.5 -> we seek 1/4

    float2 xWaveSin = frac(tex0);

    float4 xArg = xWaveSin.xyxy*PI_0p5_PI2_0p25.z - PI_0p5_PI2_0p25.x;

    // xArg.xy from -pi to +pi (x)

  // calculate 2x sin at once, using Taylor series

    float4 xArg24 = xArg*xArg; // r1.xy = r0^2.xy, r1.zw = r0^2.xy
    xArg24.zw = xArg24.xyxy*xArg24.xyxy; // r1.xy = r0^2.xy, r1.zw = r0^4.xy

    float4 xArg35 = xArg.xyxy*xArg24.xyzw; // r2.xy = r0^3.xy, r2.zw = r0^5.xy
    float4 xArg79 = xArg35*xArg24.zwzw; // r3.xy = r0^7.xy, r2.zw = r0^9.xy

    float2 waveSin = (
      xArg + 
      xArg35.xy*SIN_TAYLOR_3579.x +
      xArg35.zw*SIN_TAYLOR_3579.y +
      xArg79.xy*SIN_TAYLOR_3579.z +
      xArg79.zw*SIN_TAYLOR_3579.w
    );


    float4 xArg68 = xArg24.xyzw * xArg24.zwzw;
    float2 waveCos = (
      (float2)1 +
      xArg24.xy*COS_TAYLOR_2468.x +
      xArg24.zw*COS_TAYLOR_2468.y +
      xArg68.xy*COS_TAYLOR_2468.z +
      xArg68.zw*COS_TAYLOR_2468.w
    );

    // sin(A*x+B)' = A*cos(A*x+B)

   float2 waveDer = waveCos*VSC_WaveHeight.zw;
   float2 waveOff = waveSin*VSC_WaveHeight.xy;
   
	//finalize
	topDetect  = saturate((waveSin.x+waveSin.y)*0.5);
	waveHeight = waveOff.x + waveOff.y;
	wavemapx   = tex0.x - PI_0p5_PI2_0p25.w;
	 
	return -waveDer.xy;
}



//------------------------------------------------------------------------------------
/*
compute normal
*/
//------------------------------------------------------------------------------------
float3 ComputeNormal(float2 normal, float shoreNearDetect, float disttopoint, float3 camdir)
{
  float3 skinnedN   = float3(normal.x, 1.5, normal.y);

  //distance lerp
  const float mindist = 200.0;
  const float maxdist = 1000.0;
  
  //line equation
  const float K       = 1.0/(mindist - maxdist);
  const float Q       = -maxdist*K;
  
  float lengthlerp    = 1 - saturate(disttopoint*K + Q);
  skinnedN            = lerp(skinnedN, float3(0, 1, 0), saturate(lengthlerp + shoreNearDetect));
      
  // Modify water normal - don't modify close to shore, this fucks the normals !!!
  //const float baseInclination = 0.0f;
  //skinnedN = ModifyWaterNormal(skinnedN, camdir, baseInclination + (1.0f - baseInclination) * (1.0f - shoreNearDetect) * 0.8f /*normal inclination coefficient*/);
    
  return normalize(skinnedN);
}



//------------------------------------------------------------------------------------
/*
get grid position
*/
//------------------------------------------------------------------------------------
void VSWater(
  in float4 vPosition           : POSITION,
  in uint4 vNormal_dummy        : NORMAL,
  in int2 iTex0                : TEXCOORD0,
  in int2 iTex1_dummy          : TEXCOORD1,
  in uint4 iS_dummy             : TANGENT0,
  in uint4 iT_dummy             : TANGENT1,

  out float4 oProjectedPosition : SV_POSITION,
  
  out float4 VertexNormalFog    : TEXCOORD0,    
  out float4 VertexPosSkyLerp   : TEXCOORD1,
  out float4 tWaveMap           : TEXCOORD2,
  out float4 tNormalMap1_2      : TEXCOORD3,
  out float4 tNormalMap3        : TEXCOORD4,
  out float4 ColorProjZ         : TEXCOORD5,
  out float4 SkinnedPosition    : COLOR0,

  #ifdef WATER_REAL_REFRACTION
    out float3 DeepColor          : TEXCOORD6,
  #endif

  out float4 oShadowBuffer      : TEXCOORD7)
{ 
  // DX11 - convert to float
  float4 vTex0 = float4(iTex0.xy, 1, 1);

  TransformOutput to;
  AccomLights al;
  VertexInitLightsSimple(al);

  //not needed
  to.skinnedMatrix0 = float4(1, 0, 0, 0);
  to.skinnedMatrix1 = float4(0, 1, 0, 0);
  to.skinnedMatrix2 = float4(0, 0, 1, 0);
  to.instanceColor  = VSC_InstanceColor;
  to.instanceShadow = VSC_InstanceLandShadowIntensity.x;

  //sample grid and find height
  float4 gridSample = ComputeGridPos(vPosition.xz);
  float gridHeight = gridSample.x;
   
  //move water
  float waveHeight, topDetect, wavemapx;
  float2 normal = ComputeWaterMovement(float3(gridSample.yz,1), waveHeight, topDetect, wavemapx);
	 
  to.position         = vPosition;
  to.skinnedPosition  = vPosition;

  // Get the depth not including the wave, water depth on given position (height relative to gridHeight)
  float noWaveDepth       = gridHeight -VSC_WaterSeaLevel.x;
  float onShoreDetect     = saturate(VSC_WaterPeakWhite.x*noWaveDepth+VSC_WaterPeakWhite.y);
  float shoreNearDetect   = saturate(VSC_WaterPeakWhite.z*noWaveDepth+VSC_WaterPeakWhite.w);
  float deepWater         = saturate(VSC_WaterSeaLevel.y * noWaveDepth + VSC_WaterSeaLevel.z);
 
  tWaveMap.x = wavemapx;
  tWaveMap.y = shoreNearDetect*topDetect*0.25 + onShoreDetect;
  tWaveMap.z = shoreNearDetect;
  tWaveMap.w = deepWater;

  //by the coast, do not modify height
  float coast = saturate(1 - shoreNearDetect);

  // first we need a distance approximation, so that we can compute a LOD factor  
  float3 approxTransformedPos = mul(to.skinnedPosition, VSC_ViewMatrix);
  float dist = sqrt(dot(approxTransformedPos, approxTransformedPos));
  float fov = rsqrt(VSC_ProjMatrix._m00*VSC_ProjMatrix._m11);

  /// we dont want the lod factor to take the effect at near distances (50m)
  float lodYFactor = 1-saturate((dist-50)*fov*0.01);
  to.skinnedPosition.y += waveHeight*lodYFactor*coast*coast;
   
  // Calculate the vector from vertex position to camera (normalized)
  float3 camDirectionNN = VSC_CameraPosition.xyz - to.skinnedPosition.xyz;
  VertexPosSkyLerp.xyz  = float3(-camDirectionNN.z, camDirectionNN.x, camDirectionNN.y);
  VertexPosSkyLerp.w    = VSC_LDirectionD.w;

 
  // compute normal
  float dist2     = sqrt(dot(camDirectionNN, camDirectionNN));
  float3 normdir  = camDirectionNN/dist2;
	float3 skinnedN = ComputeNormal(normal, shoreNearDetect, dist2, normdir);

  // Fill out position and normal output variables
  to.skinnedNormal       = skinnedN;
  to.transformedPosition = float4(mul(to.skinnedPosition, VSC_ViewMatrix), 1);
  to.projectedPosition   = mul(VSC_ProjMatrix, to.transformedPosition);

  // World position
  to.worldPosition = mul(to.skinnedPosition, VSC_WorldMatrix);

  oProjectedPosition     = to.projectedPosition;
  
  //texcoords, better to do, use the xz positions
  float2 worldpos = vPosition.xz + VSC_WaterPars[0].xz - VSC_CameraPosition.xz;
  ComputeTexCoords(worldpos, tNormalMap1_2.xy, tNormalMap1_2.zw, tNormalMap3.xy, VSC_WaterPars[0].w);
 
  //normal lerp, multiply by camera height to lower the noise visibility when camera is near by the sea
  //normal lerp, multiply by camera height to lower the noise visibility when camera is near by the sea
  const float cameraunit1 = 1.0/50.0;
  const float cameraunit2 = 1.0/30.0;
  float cameraheight      = abs(VSC_CameraPosition.y);
  float cameraheightmul1  = 1.0 + (cameraheight*cameraunit1);
  float cameraheightmul2  = 1.0 + (cameraheight*cameraunit2);
  
  tNormalMap3.z = 1.6*saturate(1 - GetDistanceLERP(dist, 300.0*cameraheightmul1, 1000.0*cameraheightmul1));
  tNormalMap3.w = 1.2*saturate(1 - GetDistanceLERP(dist, 15.0*cameraheightmul2, 35.0*cameraheightmul2));
  
  // view direction for Fresnel term calculation, convert to local space
  VertexNormalFog.xyz = float3(skinnedN.x, skinnedN.z, skinnedN.y);
   
  // Apply fog
  VFog(to, VertexNormalFog.w);

  if (VSC_ShadowReceiverFlag)
  {
    VShadowReceiver(to, oShadowBuffer);
  }
  else
  {
    oShadowBuffer = 0;
  }
  
  // Include P&S lights, in al.ambient is computed light
  VLPointSpotN(to, al);

  #ifdef ARMA3_SEA_MOD
    // we need z in camera space (because we have z in camera space in depth buffer)
	  ColorProjZ.w   = to.transformedPosition.z;
  #else
	  ColorProjZ.w   = to.projectedPosition.z;
  #endif
  
  // Write lights to output, this makes just :
  #ifdef WATER_REAL_REFRACTION
    ColorProjZ.xyz = VSC_LDirectionD.rgb*CoastColorFactor;
    DeepColor      = lerp(VSC_AE.rgb, VSC_LDirectionD.rgb, deepWater);
  #else
  
    //interpolation between three values, by the water surface is VSC_LDirectionD.rgb*CoastColorFactor, in the center is
    //VSC_LDirectionD.rgb and in the deepness is VSC_AE.rgb
    const float center     = 0.7;
    const float invcenter1 = 1.0/(1.0 - center);
    const float invcenter2 = 1.0/center;

    float test = saturate(deepWater*0.5 + onShoreDetect*0.5);
    float l1 = saturate((test - center)*invcenter1);  //d = <1..center> -> <1..0>
    float l2 = saturate((center - test)*invcenter2); //d = <center..0> -> <0..1>
    
    if (l1 > 0)
    {
      ColorProjZ.xyz = lerp(VSC_LDirectionD.rgb, VSC_LDirectionD.rgb*CoastColorFactor, l1);
    } else
    {
      ColorProjZ.xyz = lerp(VSC_LDirectionD.rgb, VSC_AE.rgb, l2);    
    }
  
    //ColorProjZ.w = l1 + l2;
//    ColorProjZ.xyz = lerp(VSC_AE.rgb, VSC_LDirectionD.rgb, deepWater);
  #endif

    // pass skinned position
    SkinnedPosition = to.skinnedPosition;
}


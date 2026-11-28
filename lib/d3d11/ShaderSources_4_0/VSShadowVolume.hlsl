#include "VS.h"

#define SHADOWVOLUME_OFFSET 0.01

void VSShadowVolume(in  float4  vPosition:          POSITION,
                    in  uint4   iNormal:            NORMAL,
                    out float4  vProjectedPosition: POSITION)
{
  // convert to float
  float3 vNormal = BYTETOFLOAT01(iNormal.xyz);

  // Decompress compressed vectors  
  DecompressVector(vNormal);

  // Calculate new position
  float a;
  if (dot(vNormal, VSC_LDirectionTransformedDir.xyz) >= 0)
  {
    a = VSC_ShadowVolumeLength;
  }
  else
  {
    a = SHADOWVOLUME_OFFSET;
  }
  float4 newPosition = vPosition + float4(a, a, a, 0) * VSC_LDirectionTransformedDir;
  
  // Transform the position into view space
  float4 tPosition = float4(mul(newPosition, VSC_ViewMatrix), 1);

  // Project the position  
  vProjectedPosition = mul(VSC_ProjMatrix, tPosition);
}

float4 TransformSkinned(float4 vPosition, float4 vBlendWeights, uint4 vBlendIndices)
{
  // Skinned matrix
  float2 fRestBlendWeight = float2(1 - dot(float4(1, 1, 1, 1), vBlendWeights), 0);

  int index = (vBlendIndices - D3DCOLORtoUBYTE4(VSC_MatrixOffset)).x * SKINNING_ITEM_SIZE;
  float4 skinnedMatrix0 = VSC_SkinningInstancingData[index + 0] * vBlendWeights.x + fRestBlendWeight.xyyy;
  float4 skinnedMatrix1 = VSC_SkinningInstancingData[index + 1] * vBlendWeights.x + fRestBlendWeight.yxyy;
  float4 skinnedMatrix2 = VSC_SkinningInstancingData[index + 2] * vBlendWeights.x + fRestBlendWeight.yyxy;

  index = (vBlendIndices - D3DCOLORtoUBYTE4(VSC_MatrixOffset)).y * SKINNING_ITEM_SIZE;
  skinnedMatrix0 = VSC_SkinningInstancingData[index + 0] * vBlendWeights.y + skinnedMatrix0;
  skinnedMatrix1 = VSC_SkinningInstancingData[index + 1] * vBlendWeights.y + skinnedMatrix1;
  skinnedMatrix2 = VSC_SkinningInstancingData[index + 2] * vBlendWeights.y + skinnedMatrix2;

  index = (vBlendIndices - D3DCOLORtoUBYTE4(VSC_MatrixOffset)).z * SKINNING_ITEM_SIZE;
  skinnedMatrix0 = VSC_SkinningInstancingData[index + 0] * vBlendWeights.z + skinnedMatrix0;
  skinnedMatrix1 = VSC_SkinningInstancingData[index + 1] * vBlendWeights.z + skinnedMatrix1;
  skinnedMatrix2 = VSC_SkinningInstancingData[index + 2] * vBlendWeights.z + skinnedMatrix2;

  index = (vBlendIndices - D3DCOLORtoUBYTE4(VSC_MatrixOffset)).w * SKINNING_ITEM_SIZE;
  skinnedMatrix0 = VSC_SkinningInstancingData[index + 0] * vBlendWeights.w + skinnedMatrix0;
  skinnedMatrix1 = VSC_SkinningInstancingData[index + 1] * vBlendWeights.w + skinnedMatrix1;
  skinnedMatrix2 = VSC_SkinningInstancingData[index + 2] * vBlendWeights.w + skinnedMatrix2;

  // Skinned position
  return float4(dot(skinnedMatrix0, vPosition), dot(skinnedMatrix1, vPosition), dot(skinnedMatrix2, vPosition), 1);
}

void VSShadowVolumeSkinned(in  float4  vPosition:          POSITION,
                           in  uint4   iBlendWeights:      BLENDWEIGHT,
                           in  uint4   vBlendIndices:      BLENDINDICES,
                           in  float4  vPositionA:         POSITION1,
                           in  uint4   iBlendWeightsA:     BLENDWEIGHT1,
                           in  uint4   vBlendIndicesA:     BLENDINDICES1,
                           in  float4  vPositionB:         POSITION2,
                           in  uint4   iBlendWeightsB:     BLENDWEIGHT2,
                           in  uint4   vBlendIndicesB:     BLENDINDICES2,
                           out float4  vProjectedPosition: POSITION)
{
  // convert to float
  float4 vBlendWeights = BYTETOFLOAT01(iBlendWeights);
  float4 vBlendWeightsA = BYTETOFLOAT01(iBlendWeightsA);
  float4 vBlendWeightsB = BYTETOFLOAT01(iBlendWeightsB);
  int4 matrixOffsetInt = D3DCOLORtoUBYTE4(VSC_MatrixOffset);

  float4 skinnedPosition = TransformSkinned(vPosition, vBlendWeights, vBlendIndices);
  float4 skinnedPositionA = TransformSkinned(vPositionA, vBlendWeightsA, vBlendIndicesA);
  float4 skinnedPositionB = TransformSkinned(vPositionB, vBlendWeightsB, vBlendIndicesB);

  float3 vectorA = skinnedPositionA.xyz - skinnedPosition.xyz;
  float3 vectorB = skinnedPositionB.xyz - skinnedPosition.xyz;
  float3 n;
  n.x = vectorA.y * vectorB.z - vectorA.z * vectorB.y;
  n.y = vectorA.z * vectorB.x - vectorA.x * vectorB.z;
  n.z = vectorA.x * vectorB.y - vectorA.y * vectorB.x;
  float a;
  if (dot(n, VSC_LDirectionTransformedDir.xyz) >= 0)
  {
    a = VSC_ShadowVolumeLength;
  }
  else
  {
    a = SHADOWVOLUME_OFFSET;
  }
  float4 newSkinnedPosition = skinnedPosition + float4(a, a, a, 0) * VSC_LDirectionTransformedDir;
  float4 tPosition = float4(mul(newSkinnedPosition, VSC_ViewMatrix), 1);
  vProjectedPosition = mul(VSC_ProjMatrix, tPosition);
}

void VSShadowVolumeInstanced(in  float4  vPosition:          POSITION,
                             in  uint4   iNormal:            NORMAL,
                             in  uint4   vBlendIndices:      BLENDINDICES,
                             out float4  vProjectedPosition: POSITION)
{
  // convert to float
  float3 vNormal = BYTETOFLOAT01(iNormal.xyz);

  DecompressVector(vNormal);
  float4 skinnedMatrix0 = VSC_SkinningInstancingData[vBlendIndices.w * SKINNING_ITEM_SIZE + 0];
  float4 skinnedMatrix1 = VSC_SkinningInstancingData[vBlendIndices.w * SKINNING_ITEM_SIZE + 1];
  float4 skinnedMatrix2 = VSC_SkinningInstancingData[vBlendIndices.w * SKINNING_ITEM_SIZE + 2];
  float4 skinnedPosition;
  skinnedPosition.x = dot(skinnedMatrix0, vPosition);
  skinnedPosition.y = dot(skinnedMatrix1, vPosition);
  skinnedPosition.z = dot(skinnedMatrix2, vPosition);
  skinnedPosition.w = 1;
  float3 skinnedNormal;
  skinnedNormal.x = dot(skinnedMatrix0.xyz, vNormal);
  skinnedNormal.y = dot(skinnedMatrix1.xyz, vNormal);
  skinnedNormal.z = dot(skinnedMatrix2.xyz, vNormal);
  float a;
  if (dot(skinnedNormal, VSC_LDirectionTransformedDir.xyz) >= 0)
  {
    a = VSC_ShadowVolumeLength;
  }
  else
  {
    a = SHADOWVOLUME_OFFSET;
  }
  float4 newSkinnedPosition = skinnedPosition + float4(a, a, a, 0) * VSC_LDirectionTransformedDir;
  float4 tPosition = float4(mul(newSkinnedPosition, VSC_ViewMatrix), 1);
  vProjectedPosition = mul(VSC_ProjMatrix, tPosition);
}


//-----------------------------------------------------------------------------
// File: enf_materialparticle.cpp
//
// Desc: Superclass for our MaterialClass implementations
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "common/enf_types.h"
#include "materials/enf_materials.h"
#include "script/enf_scriptvariable.h"
#include "common/enf_diag.h"
#include "render/enf_texture.h"
#include "pipeline/enf_pipeline.h"
#include "engine/enf_engine.h"
#include "script/enf_scriptapivars.h"
//#include "enf_defaultpipeline.h"
#include "materials/enf_imaterialsystem.h"
#include "resource/enf_particleeffectinternal.h"

namespace enf
{
DECLARE_PARAM(MatParticle, MV_ALPHA_TEST);
DECLARE_PARAM(MatParticle, MV_BLEND_MODE);
DECLARE_PARAM(MatParticle, MV_COLOR);
DECLARE_PARAM(MatParticle, MV_AMBIENT);
DECLARE_PARAM(MatParticle, MV_EMISSIVE);
DECLARE_PARAM(MatParticle, MV_DIFFUSE_MAP);
DECLARE_PARAM(MatParticle, MV_NORMAL_MAP);
DECLARE_PARAM(MatParticle, MV_HEIGHT_MAP);
DECLARE_PARAM(MatParticle, MV_ZENABLE);
DECLARE_PARAM(MatParticle, MV_TILE_U);
DECLARE_PARAM(MatParticle, MV_TILE_V);
DECLARE_PARAM(MatParticle, MV_SOFTNESS);
DECLARE_PARAM(MatParticle, MV_DEPTH_SCALE);
DECLARE_PARAM(MatParticle, MV_LIGHT_SCALE);
DECLARE_PARAM(MatParticle, MV_NORMAL_LIGHT_SCALE);
DECLARE_PARAM(MatParticle, MV_CAMERA_BLEND0);
DECLARE_PARAM(MatParticle, MV_CAMERA_BLEND1);
DECLARE_PARAM(MatParticle, MV_CENTERX);
DECLARE_PARAM(MatParticle, MV_CENTERY);
DECLARE_PARAM(MatParticle, MV_SCALEX);
DECLARE_PARAM(MatParticle, MV_SCALEY);




//-----------------------------------------------------------------------
int Blend2Sort(uint blend)
{
	if(blend == BS_SRCALPHA_ADD_INVSRCALPHA.Value)
		return 10;
	if(blend == BS_ZERO_ADD_SRCCOLOR.Value)
		return 5;
	if(blend == BS_SRCALPHA_ADD_ONE.Value)
		return 1;

	return 0;
}

//-----------------------------------------------------------------------
bool MatParticle::IsCompatible(uint creationFlags) const
{
	return (creationFlags & (MCF_PARTICLE_SPRITE|MCF_PARTICLE_POINT|MCF_PARTICLE_STREAK)) != 0;
}

//-----------------------------------------------------------------------
int MatParticle::CompareMaterials(const Material* mat1, const Material* mat2) const
{
	int cmp;
	cmp = MaterialClass::CompareMaterials(mat1, mat2);
	if(cmp != 0) return cmp;

	uint v1 = Blend2Sort(mat1->GetInteger(MV_BLEND_MODE));
	uint v2 = Blend2Sort(mat2->GetInteger(MV_BLEND_MODE));

	if(v1 < v2)
		return -1;
	if(v1 > v2)
		return 1;

	cmp = mat1->CompareVariable(mat2, MV_SOFTNESS);
	if(cmp != 0) return cmp;
	cmp = mat1->CompareVariable(mat2, MV_DIFFUSE_MAP);
	if(cmp != 0) return cmp;
	cmp = mat1->CompareVariable(mat2, MV_NORMAL_MAP);
	if(cmp != 0) return cmp;
	cmp = mat1->CompareVariable(mat2, MV_ALPHA_TEST);
	if(cmp != 0) return cmp;
	cmp = mat1->CompareVariable(mat2, MV_ZENABLE);
	if(cmp != 0) return cmp;
	return 0;
}

//-----------------------------------------------------------------------
bool MatParticle::TouchTextures(Material* mat) const
{
	bool loaded = true;
	if(mat->IsVariableSet(MV_DIFFUSE_MAP))
		loaded &= mat->TouchTexture(MV_DIFFUSE_MAP);
	if(mat->IsVariableSet(MV_NORMAL_MAP))
		loaded &= mat->TouchTexture(MV_NORMAL_MAP);

	return loaded;
}

//-----------------------------------------------------------------------
MatParticle::MatParticle(MaterialSystem* imat):
	MaterialClass(imat),
	m_pNewVD(NULL)
{
	static const ParmEnum environmentenumsX[] = {
		ParmEnum("$nearestcube"),
		ParmEnum("$current")
	};
	static const ParmEnum* environmentenums[] = {
		&environmentenumsX[0], &environmentenumsX[1], NULL
	};

	static const ParmEnumInteger blendmodeenumsX[] = {
		ParmEnumInteger("colormodulate", BS_ZERO_ADD_SRCCOLOR.Value),
		ParmEnumInteger("additive_alphamodulate", BS_SRCALPHA_ADD_ONE.Value),
		ParmEnumInteger("alphablend", BS_SRCALPHA_ADD_INVSRCALPHA.Value)
	};
	static const ParmEnum* blendmodeenums[] = {
		&blendmodeenumsX[0], &blendmodeenumsX[1], &blendmodeenumsX[2], NULL
	};

	MAT_DECLARE_TEXTURE_PRI(MV_DIFFUSE_MAP, MAP_DIFFUSE, RESPRI_HIGHER, "diffusemap", "", "Diffuse texture", NULL);
	MAT_DECLARE_COLOR(MV_COLOR, "color", "1 1 1 1", "diffuse color tint. White by default", NULL);
	MAT_DECLARE_COLOR(MV_AMBIENT, "ambient", "1 1 1 1", "ambient color. White by default", NULL);
	MAT_DECLARE_COLOR(MV_EMISSIVE, "emissive", "0 0 0 0", "emissive color. Black by default", NULL);
	MAT_DECLARE_TEXTURE(MV_NORMAL_MAP, MAP_NORMAL, "normalmap", "", "Normalmap texture, if set, the fake lighting for sprites will be enabled", NULL);
	MAT_DECLARE_TEXTURE(MV_HEIGHT_MAP, MAP_HEIGHT, "heightmap", "", "Height map for parallax mapping", NULL);
	MAT_DECLARE_INTEGER(MV_ALPHA_TEST, "alphatest", "0", "slider", "0 255 1", "Defines value above which pixels are rendered", NULL);
	MAT_DECLARE_INTEGER(MV_BLEND_MODE, "blendmode", "", "combobox", NULL, "Defines how the material will be blended", blendmodeenums);
	MAT_DECLARE_BOOLEAN(MV_ZENABLE, "zenable", "true", "Enables Z-buffer compare");
	MAT_DECLARE_INTEGER(MV_TILE_U, "tileu", "1", "editbox", NULL, "Number of tiles in U axis", NULL);
	MAT_DECLARE_INTEGER(MV_TILE_V, "tilev", "1", "editbox", NULL, "Number of tiles in V axis", NULL);
	MAT_DECLARE_SCALAR(MV_SOFTNESS, "softness", "0", "slider", "0 2 0.025", "Softness", NULL);
	MAT_DECLARE_SCALAR(MV_DEPTH_SCALE, "depthscale", "0", "slider", "0 4 0.05", "Alpha to depth multiplier", NULL);
	MAT_DECLARE_SCALAR(MV_CAMERA_BLEND0, "camerablend0", "0", "slider", "0 640 1", "Distance from camera in units, where blend starts (the result is zero opacity) ", NULL);
	MAT_DECLARE_SCALAR(MV_CAMERA_BLEND1, "camerablend1", "1", "slider", "1 3200 1", "Distance from camera in units, where blend ends (the result is full opacity) ", NULL);
	MAT_DECLARE_SCALAR(MV_LIGHT_SCALE, "lightscale", "1.0", "slider", "0 1 0.01", "Light multiplicator on side of global light vector", NULL);
	MAT_DECLARE_SCALAR(MV_NORMAL_LIGHT_SCALE, "normlightscale", "1.0", "slider", "0 1 0.01", "Light multiplicator on opposite side of global light vector (when normal map is set)", NULL);
	MAT_DECLARE_SCALAR(MV_CENTERX, "centerx", "0.0", "slider", "-2 2 0.001", "Center of sprite in X axis, 0 = default center", NULL);
	MAT_DECLARE_SCALAR(MV_CENTERY, "centery", "0.0", "slider", "-2 2 0.001", "Center of sprite in Y axis, 0 = default center", NULL);
	MAT_DECLARE_SCALAR(MV_SCALEX, "scalex", "1.0", "slider", "0.01 100 0.01", "Size multiplikator of sprite in X", NULL);
	MAT_DECLARE_SCALAR(MV_SCALEY, "scaley", "1.0", "slider", "0.01 100 0.01", "Size multiplikator of sprite in Y", NULL);
}

//-----------------------------------------------------------------------
void MatParticle::OnMaterialFinalize(Material* mat)
{
	MaterialClass::OnMaterialFinalize(mat);

	mat->ClearFlags(MF_ZWRITE);

	if(mat->IsVariableSet(MV_ZENABLE))
	{
		if(mat->GetBoolean(MV_ZENABLE))
			mat->SetFlags(MF_ZENABLE);
		else
			mat->ClearFlags(MF_ZENABLE);
	}

	if(!mat->IsVariableSet(MV_DIFFUSE_MAP))
	{
		mat->SetTexture(MV_DIFFUSE_MAP, GetMaterialSystem()->GetTexture(MaterialSystem::MV_WHITE_TEXTURE));
	}

	if(!mat->IsVariableSet(MV_BLEND_MODE))
	{
		mat->SetInteger(MV_BLEND_MODE, BS_SRCALPHA_ADD_INVSRCALPHA.Value);
	}

	if (!mat->IsVariableSet(MV_TILE_U)) mat->SetDefaultValue(MV_TILE_U);
	if (mat->GetInteger(MV_TILE_U) < 1) mat->SetInteger(MV_TILE_U, 1);
	if (!mat->IsVariableSet(MV_TILE_V)) mat->SetDefaultValue(MV_TILE_V);
	if (mat->GetInteger(MV_TILE_V) < 1) mat->SetInteger(MV_TILE_V, 1);

	if(!mat->IsVariableSet(MV_COLOR))					mat->SetDefaultValue(MV_COLOR);
	if(!mat->IsVariableSet(MV_AMBIENT))					mat->SetDefaultValue(MV_AMBIENT);
	if(!mat->IsVariableSet(MV_EMISSIVE))				mat->SetDefaultValue(MV_EMISSIVE);

	if(!mat->IsVariableSet(MV_LIGHT_SCALE))			mat->SetDefaultValue(MV_LIGHT_SCALE);
	if(!mat->IsVariableSet(MV_NORMAL_LIGHT_SCALE))	mat->SetDefaultValue(MV_NORMAL_LIGHT_SCALE);
	if(!mat->IsVariableSet(MV_CAMERA_BLEND0))			mat->SetDefaultValue(MV_CAMERA_BLEND0);
	if(!mat->IsVariableSet(MV_CAMERA_BLEND1))			mat->SetDefaultValue(MV_CAMERA_BLEND1);

	if(!mat->IsVariableSet(MV_SCALEX))					mat->SetDefaultValue(MV_SCALEX);
	if(!mat->IsVariableSet(MV_SCALEY))					mat->SetDefaultValue(MV_SCALEY);
	if(!mat->IsVariableSet(MV_CENTERX))					mat->SetDefaultValue(MV_CENTERX);
	if(!mat->IsVariableSet(MV_CENTERY))					mat->SetDefaultValue(MV_CENTERY);

	if(mat->IsVariableSet(MV_ALPHA_TEST) && (mat->IsVariableFromScript(MV_ALPHA_TEST) || mat->GetInteger(MV_ALPHA_TEST) != 0))
	{
		mat->SetFlags(MF_ALPHATEST);
	}

	if(mat->IsVariableSet(MV_SOFTNESS))
	{
		if(!mat->IsVariableSet(MV_DEPTH_SCALE))
			mat->SetDefaultValue(MV_DEPTH_SCALE);
	}

	mat->DisablePass(PT_DIFFUSE_SPECULAR);
	mat->DisablePass(PT_ENVIRONMENT);
	mat->ClearFlags(MF_INSTANCING);
}

//-----------------------------------------------------------------------
MCLASS_INIT(MatParticle)
{
	m_pPColor = GET_PSH("ps_color", PSDEF(PRF_PS20));

	return EE_OK;
}


//-----------------------------------------------------------------------
void MatParticle::OnSet(Pipeline* pipeline, PassType pt)
{
#ifdef ENF_DONE
	SET_VSH_MATRIX(CV_PROJECT_MAT, m_pMaterialSystem->GetMatrix(MaterialSystem::MV_PROJECTTRANS_MATRIX));
	SET_VSH_MATRIX(CV_VIEW_MAT, GetMaterialSystem()->GetMatrix34(MaterialSystem::MV_VIEWTRANS_MATRIX));
	SET_VSH_CONST(CV_EYE_POS, m_pMaterialSystem->GetVector(MaterialSystem::MV_EYE_POS));

	//normal of plane - view direction
	const Matrix44& view = GetMaterialSystem()->GetMatrix(MaterialSystem::MV_VIEW_MATRIX);
	SET_VSH_CONST(CV_PART_EYEDIR, Vector3(view._31, view._32, view._33));
	
	//vertex texture sampling
	SET_VSFS (0, SFS_LINEAR_POINT_NONE);
	SET_VSAS (0, SAS_CLAMP_CLAMP);

	float invheight = 1.0f / PARAM_TEXTURE_SIZE;
	SET_VSH_CONST(CV_PART_TEXTURE, Vector4(0.5 * invheight, 1.5 * invheight, 2.5 * invheight, 1.0f/PARAM_TEXTURE_WIDTH));

	SET_RS(RS_CULLMODE, CULL_NONE);
	
	if(pt != PT_SHADOW)
		SET_RS(RS_COLORWRITEENABLE, ENF_DIAG_BOOL(DM_REND_COMPLEX) ? 0 : COLORWRITEENABLE_ALL);

	SET_PSH_CONST(CP_DEPTH_RANGE, GetMaterialSystem()->GetVector(MaterialSystem::MV_DEPTH_PARAMS));

	SET_TEX(5, ((DefaultPipeline*)pipeline)->GetDepthTexture());
	SET_SFS(5, SFS_POINT_POINT_NONE);
	SET_SAS(5, SAS_CLAMP_CLAMP);

	float twidth = pipeline->GetTargetWidth();
	float theight = pipeline->GetTargetHeight();

	SET_PSH_CONST(CP_PARTICLE_VIEWPORT, Vector4(1.0f / twidth, 1.0f / theight, 0.5f / twidth, 0.5f / theight));

	if(pt == PT_AMBIENT || pt == PT_AMBIENTDEPTH)
	{
		SET_TEX(6, ((DefaultPipeline*)pipeline)->GetShadowMapRT());
		SET_SAS(6, SAS_BORDER_BORDER_ONE);
		SET_SFS(6, SFS_SHADOWMAP_FILTER);
		SET_PSH_CONST (CP_SHADOWMAP_PARAMS, GetMaterialSystem()->GetVector(MaterialSystem::MV_SHADOWMAP_PARAMS));

		const Texture* tex = ((DefaultPipeline*) pipeline)->GetShadowRotTexture();

		SET_TEX(7, tex);
		SET_SAS(7, SAS_WRAP_WRAP);
		SET_SFS(7, SFS_POINT_POINT_NONE);

		//terrain shadows
		SET_VSH_CONST(CV_TERR_SHADOW, GetMaterialSystem()->GetVector(MaterialSystem::MV_TERRAIN_SHADOW));

		Texture* shadowTerrTex = GetMaterialSystem()->GetTexture(MaterialSystem::MV_TERRAIN_SHADOW_MAP);
		SET_VTEX (3, shadowTerrTex);

		//for sampling terrain shadow map which is in TEXFMT_32F, use point filtering, the linear filtering destroys the result
		if (shadowTerrTex->GetFormat() == TEXFMT_R32F)
		{
			SET_VSFS (3, SFS_POINT_POINT_NONE);
			SET_VSAS (3, SAS_CLAMP_CLAMP);
		}
		else
		{
			SET_VSFS (3, SFS_LINEAR_LINEAR_NONE);
			SET_VSAS (3, SAS_BORDER_BORDER_ZERO);
		}
	}
	else
	{
		SET_TEX(6, NULL);
	}
#endif
	SET_SAS(0, SAS_CLAMP_CLAMP);
	m_pMaterialSystem->SetFilterMode(0, FILTER_MID);

	SET_SAS(1, SAS_CLAMP_CLAMP);
	m_pMaterialSystem->SetFilterMode(0, FILTER_MID);

	g_pRender->SetVertexDeclaration(m_pNewVD);
}


//-----------------------------------------------------------------------
void MatParticleSprite::OnSet(Pipeline* pipeline, PassType pt)
{
	MatParticle::OnSet(pipeline, pt);
	SET_RS( RS_POINTSPRITEENABLE, false );
	
	//xyz billboard matrix for new GPU particles
	Vector4 billboard[2];

	billboard[0] = pipeline->GetCameraMatrix()[1];
	billboard[1] = pipeline->GetCameraMatrix()[2];
	billboard[0].z *= -1.0f;
	billboard[1].z *= -1.0f;
#ifdef ENF_DONE
	SET_VSH_CONST(CV_PART_AXES, (const float*) &billboard, 2);
#endif
}


//-----------------------------------------------------------------------
void MatParticleStreak::OnSet(Pipeline* pipeline, PassType pt)
{
	MatParticle::OnSet(pipeline, pt);
	SET_RS( RS_POINTSPRITEENABLE, false );
#ifdef ENF_DONE
	//xyz billboard matrix for new GPU particles
	Vector4 billboard[2];

	Vector3 tocamera = GetMaterialSystem()->GetMatrix(MaterialSystem::MV_INVVIEW_MATRIX).GetRow(0).v;
	Vector3 alongCamera = GetMaterialSystem()->GetMatrix(MaterialSystem::MV_INVVIEW_MATRIX).GetRow(2).v;
	tocamera = -tocamera;
	alongCamera.z = -alongCamera.z;

	billboard[0] = tocamera;
	billboard[1] = alongCamera;

	SET_VSH_CONST(CV_PART_AXES, (const float*) &billboard, 2);
#endif
}



//-----------------------------------------------------------------------
void MatParticlePoint::OnSet(Pipeline* pipeline, PassType pt)
{
	MatParticle::OnSet(pipeline, pt);
#ifdef ENF_DONE
	Vector4 epos = m_pMaterialSystem->GetVector(MaterialSystem::MV_EYE_POS);
	epos.w = (float)g_pRender->GetTargetHeight();
	SET_VSH_CONST(CV_EYEPOS_HEIGHT, epos);
#endif

	SET_RS( RS_POINTSPRITEENABLE, true );

	float maxs = enf_min(1024.0f, g_pRender->GetMaxPointSize());
	SET_RS(RS_POINTSIZE_MIN, F2DW(1.00f) );
	SET_RS(RS_POINTSIZE_MAX, F2DW(maxs) );
}


//-----------------------------------------------------------------------
MCLASS_BEGIN_PASS(MatParticle)
{
	//when alphatest is ON, it must be included in diffuse texture and normal map too, 
	//regardless specular is $nmapalpha or $alpha
	if(mat->IsFlagSet(MF_ALPHATEST))
	{
		SET_RS(RS_ALPHATESTENABLE, true);
		SET_RS(RS_ALPHAREF, mat->GetInteger(MV_ALPHA_TEST));
		SET_RS(RS_ALPHAFUNC, CMP_GREATEREQUAL);
	}
	else
	{
		SET_RS(RS_ALPHATESTENABLE, false);
	}

	if(passtype <= PT_AMBIENT)
	{
		SET_DSS(mat->IsFlagSet(MF_ZENABLE) ? DSS_ZENABLE_LE : DSS_NONE);
	}
	else if(passtype > PT_AMBIENT)
	{
		SET_DSS(mat->IsFlagSet(MF_ZENABLE) ? DSS_ZENABLE_LE : DSS_NONE);
	}

	uint blendmode = mat->GetIntegerConst(MV_BLEND_MODE);

	switch(passtype)
	{
	case PT_DEPTH:
	case PT_SHADOW:
	case PT_SHADOW_OMNIDIRECTIONAL:
		return 0;

	case PT_AMBIENT:
	case PT_AMBIENTDEPTH:
#ifdef ENF_DONE
	#ifdef ENF_DEVELOPER
		if(pipeline->GetRenderType() == RT_WIREFRAME)
		{
			SET_PSH_CONST(CP_MAT_DIFFUSE_COLOR, FColor1(1.0f));
			SET_PSH(m_pPColor);
		}
		else
	#endif
		{
			const AmbientLight& al = pipeline->GetAmbientLight(m_pContext);

			SET_PSH_CONST(CP_MAT_DIFFUSE_COLOR, ENF_DIAG_RBOOL(DM_REND_DIFF) ? mat->GetColor(MV_COLOR) * al.m_Diffuse : FColor(0.0f, 0.0f, 0.0f, 1.0f));

			//to save one instruction in PS
			//SET_PSH_CONST(CP_MAT_AMBIENT_COLOR, ENF_DIAG_RBOOL(DM_REND_AMB) ? al.m_Ambient * mat->GetColor(MV_AMBIENT) : FColor(0.0f, 0.0f, 0.0f, 1.0f));
			//SET_PSH_CONST(CP_MAT_EMISSIVE_COLOR, mat->GetColor(MV_EMISSIVE));
			FColor amb = ENF_DIAG_RBOOL(DM_REND_AMB) ? al.m_Ambient * mat->GetColor(MV_AMBIENT) : FColor(0.0f, 0.0f, 0.0f, 1.0f);
			FColor emi = mat->GetColor(MV_EMISSIVE);

			SET_PSH_CONST(CP_MAT_AMBIENT_COLOR, amb + emi);


			const AerialFog& fog = pipeline->GetWorld()->GetWorldFog();
			float eyePosZ = GetMaterialSystem()->GetVector(MaterialSystem::MV_EYE_POS).z;

			uint blendmode = mat->GetIntegerConst(MV_BLEND_MODE);
			SET_BLEND(0, BlendState(blendmode));

			FogParamsVS* fogParams = g_pRender->BeginVertexShaderConstant<FogParamsVS>(CV_FOG_PARAMS, 1);

			if(blendmode == BS_ZERO_ADD_SRCCOLOR.Value)
			{
				fogParams->m_ColorSun = fog.GetColorParams(1.0f);
				fogParams->m_ColorSunBack = fog.GetBackColorParams(1.0f, eyePosZ);
			}
			else if(blendmode == BS_SRCALPHA_ADD_ONE.Value || blendmode == BS_ONE_ADD_ONE.Value)
			{
				fogParams->m_ColorSun = fog.GetColorParams(0.0f);
				fogParams->m_ColorSunBack = fog.GetBackColorParams(0.0f, eyePosZ);
			}
			else
			{
				fogParams->m_ColorSun = fog.GetColorParams();
				fogParams->m_ColorSunBack = fog.GetBackColorParams(eyePosZ);
			}
			fogParams->m_LerpParams = fog.GetLERPParams();
			fogParams->m_MainDir = fog.GetMainDir();
			g_pRender->EndVertexShaderConstant();
		}

		SET_TEX(0, mat->GetTextureMap(MV_DIFFUSE_MAP, pipeline)->m_pTexture);
		SET_TEX(1, mat->GetTextureMap(MV_NORMAL_MAP, pipeline) ? mat->GetTextureMap(MV_NORMAL_MAP, pipeline)->m_pTexture : GetMaterialSystem()->GetTexture(MaterialSystem::MV_IDENTNORMAL_TEXTURE));

		if(mat->IsVariableSet(MV_SOFTNESS) && mat->GetScalarConst(MV_SOFTNESS) > 0.0f)
		{
			float idepthrange = 1.0f / GetMaterialSystem()->GetScalar(MaterialSystem::MV_DEPTH_RANGE);
			float soft = mat->GetScalarConst(MV_SOFTNESS) * idepthrange;
			float depth = mat->GetScalarConst(MV_DEPTH_SCALE) * idepthrange;

			SET_PSH_CONST(CP_PARTICLE_SOFTNESS, Vector4(soft, depth, 0, 0));
		}
#endif
		break;
	}

	//frames
	float u = mat->GetInteger(MV_TILE_U);
	float v = mat->GetInteger(MV_TILE_V);
#ifdef ENF_DONE
	SET_VSH_CONST(CV_PART_FRAMES, Vector4(
		1.0f/u,
		1.0f/v,
		u,
		u*v));

	//distance blend
	float neard = GetMaterialSystem()->GetScalar(MaterialSystem::MV_NEARPLANE);
	float mind = mat->GetScalar(MV_CAMERA_BLEND0) + neard;
	float maxd = mat->GetScalar(MV_CAMERA_BLEND1) + neard;

	maxd = enf_max(maxd, mind + 1.0f);

	float K, Q;
	GetLineEq(maxd, mind, K, Q);

	float lm = mat->GetScalar(MV_LIGHT_SCALE);
	SET_VSH_CONST(CV_PART_DIST_BLEND, Vector4(K, Q, 1 - lm, PARAM_TEXTURE_WIDTH));
#endif
	return 1;
}

//-----------------------------------------------------------------------
MCLASS_SET_MODEL(MatParticle)
{
	MaterialClass::SetModel(pipeline, passtype, mc);

	if(passtype == PT_AMBIENT || passtype == PT_AMBIENTDEPTH)
	{
#ifdef ENF_DONE
		SET_VSH_CONST(CV_LIGHT0_DIRECTION, pipeline->GetAmbientLight(mc).m_Direction);
#endif
	}
}

//-----------------------------------------------------------------------
MCLASS_PASS(MatParticle)
{}

//-----------------------------------------------------------------------
bool MatParticle::IsCompatible(const MaterialDescription& md)
{
	return false;
}

//-----------------------------------------------------------------------
MCLASS_INIT(MatParticleSprite)
{
	if(MatParticle::OnInit(root) != EE_OK)
		return EE_UNSUPPORTED;

	const char* options[] = {"SHADOW", "SOFT", "NRM", NULL};

	m_pAmbientNewVS = GetMaterialSystem()->CreateSuperShader("vs_particleambient2", VSDEF(PRF_VS30), options);
	m_pAmbientNewVS->PreloadAll();

	const char* options2[] = {"SHADOW", "MODULATE", "SOFT", "NRM", NULL};
	m_pAmbientPS = GetMaterialSystem()->CreateSuperShader("ps_particleambient", PSDEF(PRF_PS30), options2);
	m_pAmbientPS->PreloadAll();

	VertexDeclaration dec2;
	dec2.Begin();
		dec2.Append(0, VSDT_SHORT2, VSDU_INDICES, 0);
		dec2.Append(1, VSDT_FLOAT4, VSDU_POSITION, 0);
		dec2.Append(1, VSDT_FLOAT2, VSDU_TEXCOORD, 0);
	dec2.End();
	m_pNewVD = GetEngine()->GetRender()->CreateVertexDeclaration(dec2);

	//static vertex buffer with corners
	m_pSpriteVB = g_pRender->CreateVertexBuffer(RES_USAGE_WRITEONLY, 4, sizeof(uint), "MatParticleSprite");
	uint* vbdata = (uint*) m_pSpriteVB->Lock(0, 0, 0);
	if(vbdata)
	{
		vbdata[0]  = 0;
		vbdata[1]  = 1 + (1 << 16);

		#ifdef ENF_PS3
			vbdata[2]  = 3 + (3 << 16);
			vbdata[3]  = 2 + (2 << 16);
		#else
			vbdata[2]  = 2 + (2 << 16);
			vbdata[3]  = 3 + (3 << 16);
		#endif

		m_pSpriteVB->Unlock();
	}

	#ifdef ENF_XENON
		uint numinst = PARTICLE_MAX_SPRITES;
	#else
		uint numinst = 1;
	#endif

	#ifndef ENF_PS3
		m_pSpriteIB = g_pRender->CreateIndexBuffer(IBFMT_INDEX16, RES_USAGE_WRITEONLY, numinst*PARTICLE_SPRITE_INSTVERTS, "MatParticleSprite");
		ushort* ibdata = (ushort*) m_pSpriteIB->Lock(0, 0, 0);
		if(ibdata)
		{
			for (uint prim = 0; prim < numinst; prim++, ibdata += PARTICLE_SPRITE_INSTVERTS)
			{
				ushort offs = prim*PARTICLE_SPRITE_VBVERTS;

				#ifdef ENF_XENON
					ibdata[0]  = offs + 0;
					ibdata[1]  = offs + 1;
					ibdata[2]  = offs + 3;
					ibdata[3]  = offs + 2;
				#else
					ibdata[0]  = offs + 0;
					ibdata[1]  = offs + 1;
					ibdata[2]  = offs + 2;
					ibdata[3]  = offs + 2;
					ibdata[4]  = offs + 1;
					ibdata[5]  = offs + 3;
				#endif
			}

			m_pSpriteIB->Unlock();
		}
	#endif

	return EE_OK;
}
//-----------------------------------------------------------------------
bool MatParticleSprite::IsCompatible(const MaterialDescription& md)
{
	if(!md.m_strName.IsEmpty())
	{
		if(md.m_strName != "ParticleSprite")
			return false;
	}

	if(!(md.m_iFlags & (MCF_PARTICLE_SPRITE)))
		return false;

	return true;
}

//-----------------------------------------------------------------------
MCLASS_BEGIN_PASS(MatParticleSprite)
{
	MatParticle::BeginPass(pipeline, passtype, mat, data);
#ifdef ENF_DONE
	//size and center offset
	SET_VSH_CONST(CV_PART_SPRITE_SIZE, Vector4(
		mat->GetScalar(MV_SCALEX),
		mat->GetScalar(MV_SCALEY),
		mat->GetScalar(MV_CENTERX),
		mat->GetScalar(MV_CENTERY)));

	//common states
	switch(passtype)
	{
	case PT_DEPTH:
	case PT_SHADOW:
	case PT_SHADOW_OMNIDIRECTIONAL:
		return 0;

	case PT_AMBIENT:
	case PT_AMBIENTDEPTH:
		{
			//(NdotL + 1)*0.5*LightMul + LightAdd  = NdotL*0.5*LM + 0.5*LM + LA = NdotL*(0.5*LM) + (0.5*LM + LA)
			float lm = mat->GetScalar(MV_NORMAL_LIGHT_SCALE);
			float la = 1.0f - lm;

			SET_PSH_CONST(CP_PARTICLE_LIGHT, Vector3(
				0.5f*lm,
				0.5f*lm + la,
				0.0f));

			bool soft = mat->IsVariableSet(MV_SOFTNESS) && mat->GetScalarConst(MV_SOFTNESS) > 0.0f;
			bool nrm = mat->IsVariableSet(MV_NORMAL_MAP) && mat->GetTextureMap(MV_NORMAL_MAP, pipeline);
			
			SET_VSH(m_pAmbientNewVS->GetVShader(mat->IsShadowReceiver(), soft, nrm));

			uint blendmode = mat->GetInteger(MV_BLEND_MODE);

			#ifdef ENF_DEVELOPER
				if(pipeline->GetRenderType() != RT_WIREFRAME)
			#endif

			SET_PSH(m_pAmbientPS->GetPShader(mat->IsShadowReceiver(), blendmode == BS_ZERO_ADD_SRCCOLOR.Value, soft, nrm));

			break;
		}
	}
#endif
	return 1;
}

//-----------------------------------------------------------------------
MCLASS_SET_MODEL(MatParticleSprite)
{
	MatParticle::SetModel(pipeline, passtype, mc);
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void SetInstanceData(void* data)
{
	//instance data
	const EmitorDef* def = (const EmitorDef*) data;

	SET_VTEX(0, g_pParticleSystem->GetParamTexture(def->GetIndex()));
#ifdef ENF_DONE
	SET_VSH_CONST(CV_PART_INSTANCEDATA, Vector3(
		def->GetGPUIndex(),
		def->m_fSize,
		def->m_fStretch));
#endif
}


//-----------------------------------------------------------------------
MCLASS_PASS(MatParticleSprite)
{
	SetInstanceData(mesh->m_pUserData);

	//render
	uint instanceoffs = static_cast<uint>(reinterpret_cast<uint64>(mesh->m_pVB[1]));

	g_pRender->SetStreamSource(0, m_pSpriteVB, 0, sizeof(uint));
	g_pRender->SetStreamSource(1, mesh->m_pVB[0], instanceoffs, mesh->m_iStride[0]);
	g_pRender->SetIndices(m_pSpriteIB);

	#if defined(ENF_XENON)
		SET_VSH_CONST(CV_INSTANCE_PARAM, Vector4(mesh->m_iNumVertices, mesh->m_iFirstVertex, 0, 0));
	#endif

	#ifdef ENF_PS3
		m_pMaterialSystem->DrawInstancedMesh(mesh, mesh->m_iNumInstances);
	#else
		m_pMaterialSystem->DrawIndexedInstancedMesh(mesh, mesh->m_iNumInstances);
	#endif
	
	g_pRender->SetStreamSource(1, NULL, 0, 0);

	#ifndef ENF_XENON
		g_pRender->SetStreamSourceFreqOp(0, SFO_NONE, 1);
		g_pRender->SetStreamSourceFreqOp(1, SFO_NONE, 1);
	#endif
}


//-----------------------------------------------------------------------
MCLASS_INIT(MatParticlePoint)
{
	if(MatParticle::OnInit(root) != EE_OK)
		return EE_UNSUPPORTED;

	const char* defines[] = {"POINT", NULL};
	const char* options[] = {"SHADOW", NULL};
	m_pAmbientNewVS = GetMaterialSystem()->CreateSuperShader("vs_particleambient2", VSDEF(PRF_VS30), options, defines);
	m_pAmbientNewVS->PreloadAll();

	const char* options2[] = {"SHADOW", "MODULATE", NULL};
	m_pAmbientPS = GetMaterialSystem()->CreateSuperShader("ps_particleambient", PSDEF(PRF_PS30), options2, defines);
	m_pAmbientPS->PreloadAll();

	VertexDeclaration dec2;
	dec2.Begin();
		dec2.Append(0, VSDT_FLOAT4, VSDU_POSITION, 0);
		dec2.Append(0, VSDT_FLOAT1, VSDU_TEXCOORD, 0);
	dec2.End();
	m_pNewVD = GetEngine()->GetRender()->CreateVertexDeclaration(dec2);

	return EE_OK;
}
//-----------------------------------------------------------------------
bool MatParticlePoint::IsCompatible(const MaterialDescription& md)
{
	if(!md.m_strName.IsEmpty())
	{
		if(md.m_strName != "ParticlePoint")
			return false;
	}

	if(!(md.m_iFlags & MCF_PARTICLE_POINT))
		return false;

	return true;
}

//-----------------------------------------------------------------------
MCLASS_BEGIN_PASS(MatParticlePoint)
{
	MatParticle::BeginPass(pipeline, passtype, mat, data);

	//common states
	SET_RS( RS_POINTSPRITEENABLE, true );

	switch(passtype)
	{
	case PT_DEPTH:
	case PT_SHADOW:
	case PT_SHADOW_OMNIDIRECTIONAL:
		return 0;

	case PT_AMBIENT:
	case PT_AMBIENTDEPTH:
		{
			SET_VSH(m_pAmbientNewVS->GetVShader(mat->IsShadowReceiver()));

			uint blendmode = mat->GetInteger(MV_BLEND_MODE);
			
			#ifdef ENF_DEVELOPER
				if(pipeline->GetRenderType() != RT_WIREFRAME)
			#endif

			SET_PSH(m_pAmbientPS->GetPShader(mat->IsShadowReceiver(), blendmode == ((BLEND_SRCCOLOR << 16) | BLEND_ZERO)));
		}
		break;
	}

	return 1;
}

//-----------------------------------------------------------------------
MCLASS_SET_MODEL(MatParticlePoint)
{
	MatParticle::SetModel(pipeline, passtype, mc);
}

//-----------------------------------------------------------------------
MCLASS_PASS(MatParticlePoint)
{
	SetInstanceData(mesh->m_pUserData);

	g_pRender->SetStreamSource(0, mesh->m_pVB[0], 0, mesh->m_iStride[0]);
	g_pRender->SetIndices(mesh->m_pIB);

	m_pMaterialSystem->DrawMesh(mesh);
}



//-----------------------------------------------------------------------
MCLASS_INIT(MatParticleStreak)
{
	if(MatParticle::OnInit(root) != EE_OK)
		return EE_UNSUPPORTED;

	const char* defines[] = {"STREAK", NULL};
	const char* options[] = {"SHADOW", "SOFT", NULL};
	m_pAmbientNewVS = GetMaterialSystem()->CreateSuperShader("vs_particleambient2", VSDEF(PRF_VS30), options, defines);
	m_pAmbientNewVS->PreloadAll();

	const char* options2[] = {"SHADOW", "MODULATE", "SOFT", NULL};
	m_pAmbientPS = GetMaterialSystem()->CreateSuperShader("ps_particleambient", PSDEF(PRF_PS30), options2);
	m_pAmbientPS->PreloadAll();

	VertexDeclaration dec2;
	dec2.Begin();
		dec2.Append(0, VSDT_SHORT2, VSDU_INDICES, 0);
		dec2.Append(1, VSDT_FLOAT4, VSDU_POSITION, 0);
		dec2.Append(1, VSDT_FLOAT4, VSDU_TEXCOORD, 0);
	dec2.End();
	m_pNewVD = GetEngine()->GetRender()->CreateVertexDeclaration(dec2);

	//static vertex buffer with corners
	#ifdef ENF_PS3
		m_pStreakVB = g_pRender->CreateVertexBuffer(RES_USAGE_WRITEONLY, 12, sizeof(uint), "MatParticleStreak");
		uint* vbdata = (uint*) m_pStreakVB->Lock(0, 0, 0);
		if(vbdata)
		{
			vbdata[0]  = 0;
			vbdata[1]  = 1 + (1 << 16);
			vbdata[2]  = 3 + (3 << 16);
			vbdata[3]  = 2 + (2 << 16);

			vbdata[4]  = 2 + (2 << 16);
			vbdata[5]  = 3 + (3 << 16);
			vbdata[6]  = 5 + (5 << 16);
			vbdata[7]  = 4 + (4 << 16);

			vbdata[8]  = 4 + (4 << 16);
			vbdata[9]  = 5 + (5 << 16);
			vbdata[10] = 7 + (7 << 16);
			vbdata[11] = 6 + (6 << 16);

			m_pStreakVB->Unlock();
		}
	#else
		m_pStreakVB = g_pRender->CreateVertexBuffer(RES_USAGE_WRITEONLY, 8, sizeof(uint), "MatParticleStreak");
		uint* vbdata = (uint*) m_pStreakVB->Lock(0, 0, 0);
		if(vbdata)
		{
			vbdata[0]  = 0;
			vbdata[1]  = 1 + (1 << 16);
			vbdata[2]  = 2 + (2 << 16);
			vbdata[3]  = 3 + (3 << 16);
			vbdata[4]  = 4 + (4 << 16);
			vbdata[5]  = 5 + (5 << 16);
			vbdata[6]  = 6 + (6 << 16);
			vbdata[7]  = 7 + (7 << 16);

			m_pStreakVB->Unlock();
		}
	#endif

	//index buffer, on XBOX the indices must be duplicated as the vertices are vfetched manually in VS
	#ifdef ENF_XENON
		uint numinst = PARTICLE_MAX_STREAKS;
	#else
		uint numinst = 1;
	#endif

	#ifndef ENF_PS3
		m_pStreakIB = g_pRender->CreateIndexBuffer(IBFMT_INDEX16, RES_USAGE_WRITEONLY, numinst*PARTICLE_STREAK_INSTVERTS, "MatParticleStreak");
		ushort* ibdata = (ushort*) m_pStreakIB->Lock(0, 0, 0);
		if(ibdata)
		{
			for (uint prim = 0; prim < numinst; prim++, ibdata += PARTICLE_STREAK_INSTVERTS)
			{
				ushort offs = prim*PARTICLE_STREAK_VBVERTS;

				#ifdef ENF_XENON
					ibdata[0]  = offs + 0;
					ibdata[1]  = offs + 1;
					ibdata[2]  = offs + 3;
					ibdata[3]  = offs + 2;
					ibdata[4]  = offs + 2;
					ibdata[5]  = offs + 3;
					ibdata[6]  = offs + 5;
					ibdata[7]  = offs + 4;
					ibdata[8]  = offs + 4;
					ibdata[9]  = offs + 5;
					ibdata[10] = offs + 7;
					ibdata[11] = offs + 6;
				#else
					ibdata[0]  = offs + 0;
					ibdata[1]  = offs + 1;
					ibdata[2]  = offs + 2;
					ibdata[3]  = offs + 2;
					ibdata[4]  = offs + 1;
					ibdata[5]  = offs + 3;
					ibdata[6]  = offs + 2;
					ibdata[7]  = offs + 3;
					ibdata[8]  = offs + 4;
					ibdata[9]  = offs + 4;
					ibdata[10] = offs + 3;
					ibdata[11] = offs + 5;
					ibdata[12] = offs + 4;
					ibdata[13] = offs + 5;
					ibdata[14] = offs + 6;
					ibdata[15] = offs + 6;
					ibdata[16] = offs + 5;
					ibdata[17] = offs + 7;
				#endif
			}

			m_pStreakIB->Unlock();
		}
	#endif


	return EE_OK;
}
//-----------------------------------------------------------------------
bool MatParticleStreak::IsCompatible(const MaterialDescription& md)
{
	if(!md.m_strName.IsEmpty())
	{
		if(md.m_strName != "ParticleStreak")
			return false;
	}

	if(!(md.m_iFlags & MCF_PARTICLE_STREAK))
		return false;

	return true;
}

//-----------------------------------------------------------------------
MCLASS_BEGIN_PASS(MatParticleStreak)
{
	MatParticle::BeginPass(pipeline, passtype, mat, data);

	switch(passtype)
	{
	case PT_DEPTH:
	case PT_SHADOW:
	case PT_SHADOW_OMNIDIRECTIONAL:
		return 0;

	case PT_AMBIENT:
	case PT_AMBIENTDEPTH:
		{
			bool soft = mat->IsVariableSet(MV_SOFTNESS) && mat->GetScalarConst(MV_SOFTNESS) > 0.0f;
			SET_VSH(m_pAmbientNewVS->GetVShader(mat->IsShadowReceiver(), soft));
			uint blendmode = mat->GetInteger(MV_BLEND_MODE);

			#ifdef ENF_DEVELOPER
				if(pipeline->GetRenderType() != RT_WIREFRAME)
			#endif

			SET_PSH(m_pAmbientPS->GetPShader(mat->IsShadowReceiver(), blendmode == ((BLEND_SRCCOLOR << 16) | BLEND_ZERO), soft));
		}
		break;
	}

	return 1;
}

//-----------------------------------------------------------------------
MCLASS_SET_MODEL(MatParticleStreak)
{
	MatParticle::SetModel(pipeline, passtype, mc);
}


//-----------------------------------------------------------------------
MCLASS_PASS(MatParticleStreak)
{
	//instance data
	SetInstanceData(mesh->m_pUserData);

	//render
	uint instanceoffs = static_cast<uint>(reinterpret_cast<uint64>(mesh->m_pVB[1]));

	g_pRender->SetStreamSource(0, m_pStreakVB, 0, sizeof(uint));
	g_pRender->SetStreamSource(1, mesh->m_pVB[0], instanceoffs, mesh->m_iStride[0]);
	g_pRender->SetIndices(m_pStreakIB);

	#if defined(ENF_XENON)
		SET_VSH_CONST(CV_INSTANCE_PARAM, Vector4(mesh->m_iNumVertices, mesh->m_iFirstVertex, 0, 0));
	#endif

	#ifdef ENF_PS3
		m_pMaterialSystem->DrawInstancedMesh(mesh, mesh->m_iNumInstances);
	#else
		m_pMaterialSystem->DrawIndexedInstancedMesh(mesh, mesh->m_iNumInstances);
	#endif

	g_pRender->SetStreamSource(1, NULL, 0, 0);

	#ifndef ENF_XENON
		g_pRender->SetStreamSourceFreqOp(0, SFO_NONE, 1);
		g_pRender->SetStreamSourceFreqOp(1, SFO_NONE, 1);
	#endif
}


} //namespace enf

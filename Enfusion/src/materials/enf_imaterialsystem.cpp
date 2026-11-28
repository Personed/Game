//-----------------------------------------------------------------------------
// File: enf_imaterialsystem.cpp
//
// Desc: Internal class for material system
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "engine/enf_engine.h"
#include "render/enf_rendmain.h"
#include "render/enf_texture.h"
#include "materials/enf_imaterialsystem.h"
//#include "script/enf_scriptapivars.h"
#include "pipeline/enf_pipeline.h"
#include "tinyxml/tinyxml.h"
#include "common/enf_diag.h"
#include "common/enf_pointerhash.h"
#include "common/enf_quicksort.h"
#include "render/enf_supershader.h"
//#include "script/enf_scriptapi.h"
#include "common/enf_staticbuffer.h"

namespace enf
{

class PUBLIC_API ISuperShader: public SuperShader
{
private:
	friend class IMaterialSystem;

	enum OptionType
	{
		OT_BOOL,
		OT_INT
	};

	struct ShaderOption
	{
		OptionType	Type;
		uint			Size; ///<it's size of enum, or 1 when bool
		const char*	Name;
	};

	CArray< ComPtr<RenderShader> >	Shaders;
	DefineSet	Defines;
	CStr Name;
	ProfileType	Profile;
	IMaterialSystem*	MSystem;
	SArray<ShaderOption, MAX_OPTIONS> Options;

	~ISuperShader();

	void SetOptions(const char* const* options);

public:
	ENF_DECLARE_MMOBJECT(ISuperShader);

	ISuperShader(CStr name, ProfileType profile);

	uint GetHashValue() const;
	int Compare(const SuperShader* ss) const;

	VertexShader* _GetVShader(uint index) const
	{
		RenderShader* shader = Shaders[index];
		enf_assert(shader != NULL);
		enf_assert(shader->HasType(VertexShaderTypeID));	
		return (VertexShader*)shader;
	}
	PixelShader* _GetPShader(uint index) const
	{
		RenderShader* shader = Shaders[index];
		enf_assert(shader != NULL);
		enf_assert(shader->HasType(PixelShaderTypeID));
		return (PixelShader*)shader;
	}

	void Preload(int p0 = 0, int p1 = 0, int p2 = 0, int p3 = 0, int p4 = 0, int p5 = 0, int p6 = 0);
	void PreloadAll();
};

//-----------------------------------------------------------------------
uint ISuperShader::GetHashValue() const
{
	uint hash = 0;

	hash += Name.GetHashValue();
	for(uint n = 0; n < Options.GetCardinality(); n++)
	{
		hash += HashPointer(Options[n].Name);
	}

	hash += Defines.GetHashValue();

	return hash;
}

//-----------------------------------------------------------------------
int ISuperShader::Compare(const SuperShader* ssh) const
{
	enf_assert(ssh != NULL);
	ISuperShader* ss = (ISuperShader*)ssh;

	if(Profile < ss->Profile)
		return -1;
	if(Profile > ss->Profile)
		return 1;

	int cmp = Name.cmp(ss->Name);
	if(cmp != 0) return cmp;

	cmp = Defines.Compare(ss->Defines);
	if(cmp != 0) return cmp;

	if(Options.GetCardinality() < ss->Options.GetCardinality())
		return -1;
	if(Options.GetCardinality() > ss->Options.GetCardinality())
		return 1;

	for(uint n = 0; n < Options.GetCardinality(); n++)
	{
		if(Options[n].Name < ss->Options[n].Name)
			return -1;
		if(Options[n].Name > ss->Options[n].Name)
			return 1;
		if(Options[n].Type < ss->Options[n].Type)
			return -1;
		if(Options[n].Type > ss->Options[n].Type)
			return 1;
		if(Options[n].Size < ss->Options[n].Size)
			return -1;
		if(Options[n].Size > ss->Options[n].Size)
			return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------
void ISuperShader::PreloadAll()
{
	int maxval[MAX_OPTIONS];
	ZeroMem(maxval, sizeof(maxval));

	for(uint n = 0; n < Options.GetCardinality(); n++)
		maxval[n] = Options[n].Size - 1;

	Preload(maxval[0], maxval[1], maxval[2], maxval[3], maxval[4], maxval[5], maxval[6]);
}

//-----------------------------------------------------------------------
void ISuperShader::Preload(int p0, int p1, int p2, int p3, int p4, int p5, int p6)
{
	g_pRender->BeginShaderCompile();

	if(Shaders.IsEmpty())
	{
		uint num = 1;
		for(uint n = 0; n < Options.GetCardinality(); n++)
			num *= Options[n].Size;
		Shaders.SetSize(num);
	}

	for(int c6 = 0; c6 <= p6; c6++)
	{
		for(int c5 = 0; c5 <= p5; c5++)
		{
			for(int c4 = 0; c4 <= p4; c4++)
			{
				for(int c3 = 0; c3 <= p3; c3++)
				{
					for(int c2 = 0; c2 <= p2; c2++)
					{
						for(int c1 = 0; c1 <= p1; c1++)
						{
							for(int c0 = 0; c0 <= p0; c0++)
							{
								uint index = Params2Index(c0, c1, c2, c3, c4, c5, c6);

								//already preloaded
								if(Shaders[index] != NULL)
									continue;

								int vals[MAX_OPTIONS] = {c0, c1, c2, c3, c4, c5, c6};
								SArray<Define, MAX_OPTIONS + 20> defines;

								for(uint n = 0; n < Defines.GetCardinality(); n++)
									defines.Insert(Defines[n]);

								for(uint n = 0; n < Options.GetCardinality(); n++)
								{
									if(vals[n] == 0 && Options[n].Type == OT_BOOL)
										continue;

									if(Options[n].Type == OT_BOOL)
										defines.Insert(Define(Options[n].Name));
									else
										defines.Insert(Define(Options[n].Name, vals[n]));
								}
								Shaders[index] = g_pRender->GetShader(Name, Profile, defines.GetArray(), defines.GetCardinality());
							}
						}
					}
				}
			}
		}
	}
	g_pRender->EndShaderCompile();
}

//-----------------------------------------------------------------------
ISuperShader::~ISuperShader()
{
	//shaders are released as they are in CArray<ComPtr<>>

	if(MSystem)
	{
		if(MSystem->m_SuperShaders.Remove(this))
		{
			MSystem->m_SuperShadersHash.Remove(this);
		}
		MSystem = NULL;
	}
}

//-----------------------------------------------------------------------
ISuperShader::ISuperShader(CStr name, ProfileType profile):
	Profile(profile),
	MSystem(NULL)
{
	SString<> cname = name;
	enf_assert2(strchr(cname.cstr(), '.') == NULL, "File extension detected!");
	cname.lower();
	Name = g_pStatic->AddString(cname.cstr());

	for(uint n = 0; n < (MAX_OPTIONS - 1); n++)
		Multiply[n] = 1;
}

//-----------------------------------------------------------------------
void ISuperShader::SetOptions(const char* const* options)
{
	enf_assert(options != NULL);

	int num = 0;
	int mult = 1;

	while(*options)
	{
		const char* str = *options;

		const char* bra = strchr(str, '[');
		const char* ket = (bra != NULL) ? strchr(bra, ']') : NULL;

		ShaderOption opt;

		if(bra && ket)
		{
			opt.Type = OT_INT;
			opt.Size = atol(bra + 1);
			enf_assert(opt.Size >= 1);
			if(opt.Size < 1) opt.Size = 1;
			opt.Name = g_pStatic->AddString(str, bra - str);
		}
		else
		{
			opt.Type = OT_BOOL;
			opt.Size = 2;
			opt.Name = g_pStatic->AddString(str);
		}

		if(num > 0)
			Multiply[num - 1] = mult;

		mult *= opt.Size;
		Options.Insert(opt);
		options++;
		num++;
	}
}

//-----------------------------------------------------------------------
SuperShader* IMaterialSystem::CreateSuperShader(CStr name, ProfileType prof, const char* const* options, const char* const* defines)
{
	CMutexLock lock(m_Mutex);
	ComPtr<ISuperShader> ss = ENF_NEW ISuperShader(name, prof);

	if(defines)
		ss->Defines.Append(defines);

	if(options)
		ss->SetOptions(options);

	SuperShader* xss;
	if(m_SuperShadersHash.Find(ss, xss))
	{
		xss->AddRef();
		return xss;
	}
/*
#ifdef ENF_DEVELOPER
	SString<> sname = "shaders/";
	sname += name;
	sname += ".hlsl";
	if(!GetFileSystem()->FileExists(sname.cstr()))
	{
		enf::LogF(LL_ERROR, "Can't create supershader '%s'. Source '%s' is missing", name.cstr(), sname.cstr());
	}
#endif
*/
	ss->MSystem = this;
	m_SuperShaders.Insert(ss);
	m_SuperShadersHash.Insert(ss, ss);
	ss.AddRef();
	return ss;
}

//-----------------------------------------------------------------------
void IMaterialSystem::SetCull(const Material* mat) const
{
	if(m_bInMirror)
		SET_RS(RS_CULLMODE, mat->IsCulling() ? CULL_CW : CULL_NONE);
	else
		SET_RS(RS_CULLMODE, mat->IsCulling() ? CULL_CCW : CULL_NONE);
}

//-----------------------------------------------------------------------
void IMaterialSystem::SetCull(bool doCull) const
{
	SET_RS(RS_CULLMODE, doCull ? (m_bInMirror ? CULL_CW : CULL_CCW) : CULL_NONE);
}

//-----------------------------------------------------------------------
Material* IMaterialSystem::GetMaterial(CStr matname, uint flags)
{
	CMutexLock lock(m_Mutex);

	Material* best = m_MaterialsHash.Find(matname.cstr());
	MaterialDescription mdesc;
	mdesc.m_iFlags = flags;

	if(best && best->m_iNumVariables == 0 && best->m_pMClass == NULL) //it's dummy material. Cannot clone anyway
	{
		best->AddRef();
		return best;
	}

	for(Material* mat = best; mat != NULL; mat = mat->m_pNextClone)
	{
		//abstract base material
		if(mat->GetMaterialClass()->IsAbstract())
			continue;

		if(mat->GetMaterialClass()->IsCompatible(mdesc))
		{
			if(mat->m_iCreationFlags == flags)
			{
				mat->AddRef();
				return mat;
			}
			//if we're the first, don't clone, just change creation flags
			//FIXME: this do not work. It changes already referenced material. Is the refcount from 0?
			/*if(mat->m_iCreationFlags == 0 && mat->RefCount() == 1)
			{
				mat->m_iCreationFlags = flags;
				mat->AddRef();
				return mat;
			}*/
		}
	}

	if(best)
	{
		MaterialClass* mr = FindMaterialClass(mdesc, best->GetMaterialClass() ? best->GetMaterialClass()->GetName() : NULL);
	
		if(mr)
		{

			best = best->CreateClone(flags);
			best->SetMaterialClass(mr);
			best->Finalize();
			return best;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------
Texture* IMaterialSystem::GetIdentityColorMap(const FColor& color, uint flags)
{
	flags &= (TEXTR_CUBE|TEXTR_VERTEXTEXTURE);

	int index = m_IdentityTextures.GetIndexOf(IdentityTextureItem(NULL, color, flags));

	if(index >= 0)
	{
		return m_IdentityTextures[index].m_pTexture;
	}

	TextureFormat texFmt = TEXFMT_A8R8G8B8;
#ifdef ENF_PS3
	if(flags & TEXTR_VERTEXTEXTURE)
		texFmt = TEXFMT_R32F;
#endif

	Texture* tex = g_pRender->CreateTextureWithFormat(NULL, 1, 1, texFmt, flags, MAP_DIFFUSE);
	tex->FillColor(color, 0, 0, 1, 1);
	m_IdentityTextures.Insert(IdentityTextureItem(tex, color, flags));
	return tex;
}

//-----------------------------------------------------------------------
void IMaterialSystem::SetBias(float bias)
{
#ifndef NO_REDUNDANT_RS_CHECK
	if(bias == m_fLastBias)
		return;
#endif

	m_fLastBias = bias;

	if(bias != 0)
	{
		#if defined(ENF_OGL)
		#endif
	}
	else
	{
		#if defined(ENF_OGL)
		#endif
	}
}

//-----------------------------------------------------------------------
void IMaterialSystem::PopProjection()
{
	enf_assert(m_iProjStackPos > 0);
	m_iProjStackPos--;
	SetProjection(
		m_ProjStack[m_iProjStackPos].Left,
		m_ProjStack[m_iProjStackPos].Top,
		m_ProjStack[m_iProjStackPos].Width,
		m_ProjStack[m_iProjStackPos].Height,
		m_ProjStack[m_iProjStackPos].ProjMatrix,
		m_ProjStack[m_iProjStackPos].NearPlane,
		m_ProjStack[m_iProjStackPos].FarPlane
		);
}

//-----------------------------------------------------------------------
bool IMaterialSystem::PushProjection(int left, int top, int width, int height, const Matrix44& projmatrix, float nearplane, float farplane)
{
	enf_assert(m_iProjStackPos < MAX_CAMERA_STACK);
	if(m_iProjStackPos >= MAX_CAMERA_STACK)
		return false;

	m_iProjStackPos++;
	SetProjection(left, top, width, height, projmatrix, nearplane, farplane);
	return true;
}

//-----------------------------------------------------------------------
bool IMaterialSystem::PushCamera(const Matrix43& viewmatrix, const Matrix43& invviewmatrix)
{
	enf_assert(m_iCameraStackPos < MAX_CAMERA_STACK);
	if(m_iCameraStackPos >= MAX_CAMERA_STACK)
		return false;
	m_iCameraStackPos++;
	SetupCamera(viewmatrix, invviewmatrix);
	return true;
}

//-----------------------------------------------------------------------
void IMaterialSystem::PopCamera()
{
	enf_assert(m_iCameraStackPos > 0);
	--m_iCameraStackPos;
	SetupCamera(m_CameraStack[m_iCameraStackPos][0], m_CameraStack[m_iCameraStackPos][1]);
}


//-----------------------------------------------------------------------
void IMaterialSystem::SetProjection(int left, int top, int width, int height, const Matrix44& projmatrix, float nearplane, float farplane)
{
	m_ProjStack[m_iProjStackPos].Left = left;
	m_ProjStack[m_iProjStackPos].Top = top;
	m_ProjStack[m_iProjStackPos].Width = width;
	m_ProjStack[m_iProjStackPos].Height = height;
	m_ProjStack[m_iProjStackPos].ProjMatrix = projmatrix;
	m_ProjStack[m_iProjStackPos].NearPlane = nearplane;
	m_ProjStack[m_iProjStackPos].FarPlane = farplane;


	if(width != 0 && height != 0)
	{
		SetScalar(MV_ASPECT, (float)width / (float)height);
		g_pRender->SetViewport(RenderViewport(left, top, width, height, 0.0f, 1.0f));
	}
	SetScalar(MV_NEARPLANE, nearplane);
	SetScalar(MV_FARPLANE, farplane);

	float deltaplane = farplane - nearplane;
	float rangescale = farplane / GetScalar(MaterialSystem::MV_DEPTH_RANGE);
	float nearXrange = nearplane * rangescale;
	//it happens on unused stack, and crashes on Xenon
	if(nearXrange == 0)
		nearXrange = 1.0f;
	SetVector(MV_DEPTH_PARAMS, Vector4(-deltaplane/nearXrange, farplane/nearXrange, 0, 0));

	SetMatrix(MV_PROJECT_MATRIX, projmatrix);
#ifdef ENF_DONE
	SetMatrix(MV_PROJECTTRANS_MATRIX, projmatrix.GetTranspose());
#endif
}

//-----------------------------------------------------------------------
void IMaterialSystem::EndFrame()
{
	AssertMainThread();
	for(uint m = 0; m < this->m_MatClasses.GetCardinality(); m++)
	{
		m_MatClasses[m]->EndFrame();
	}
}

//-----------------------------------------------------------------------
void IMaterialSystem::OnBeginFrame()
{
	AssertMainThread();

#ifdef _DEBUG
	//reset all variables - makes it easier do discover usage of stale values
//	for(uint n = 0; n < m_ScalarVariables.GetCardinality(); n++)
//		m_ScalarVariables[n] = 0.0f;
	for(uint n = 0; n < m_Matrix44Variables.GetCardinality(); n++)
		m_Matrix44Variables[n].Zero();
	for(uint n = 0; n < m_Matrix34Variables.GetCardinality(); n++)
		m_Matrix34Variables[n].Zero();
	for(uint n = 0; n < m_Vector4Variables.GetCardinality(); n++)
		m_Vector4Variables[n].Zero();
#endif

	double dtime = GetEngine()->GetTime();
	SetScalar(MV_TIME, dtime * 0.001f);
}

//-----------------------------------------------------------------------
class CompareMaterials
{
public:
	int operator()(const Material* mat1, const Material* mat2) const
	{
		if(mat1->GetMaterialClass() < mat2->GetMaterialClass())
			return -1;
		if(mat1->GetMaterialClass() > mat2->GetMaterialClass())
			return 1;

		int cmp = mat1->GetMaterialClass()->CompareMaterials(mat1, mat2);
		if(cmp != 0)
			return cmp;

		return mat1->GetName().cmp(mat2->GetName());
	}
};

//-----------------------------------------------------------------------
void IMaterialSystem::SetupCamera(const Matrix43& viewmatrix, const Matrix43& invviewmatrix)
{
#ifdef ENF_DONE
	m_CameraStack[m_iCameraStackPos][0] = viewmatrix;
	m_CameraStack[m_iCameraStackPos][1] = invviewmatrix;
	Matrix44 mview = invviewmatrix;
	Matrix44 invmview = viewmatrix;

	SetMatrix(MV_VIEW_MATRIX, mview);
	Matrix34 tmview = mview;
	SetMatrix34(MV_VIEWTRANS_MATRIX, tmview);

	SetMatrix(MV_INVVIEW_MATRIX, invmview);
	SetMatrix(MV_INVVIEWTRANS_MATRIX, invmview.GetTranspose());

	Matrix44 cmat = viewmatrix;
	SetMatrix(MV_CAMERA_MATRIX, cmat);
	SetVector(MV_EYE_POS, viewmatrix[3]);

	SetMatrix(MV_PROJVIEW_MATRIX, mview * GetMatrix(MV_PROJECT_MATRIX));

	// Set up a rotation matrix to orient the billboard towards the camera.
	SetMatrix(MV_BILLBOARD_MATRIX, Matrix44(
		viewmatrix[0].x, viewmatrix[0].y, -viewmatrix[0].z, 0.0f,
		viewmatrix[1].x, viewmatrix[1].y, -viewmatrix[1].z, 0.0f,
		viewmatrix[2].x, viewmatrix[2].y, -viewmatrix[2].z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f));
#endif
}

//-----------------------------------------------------------------------
void IMaterialSystem::BeginFrame(const Matrix43& viewmatrix, RenderType type)
{
	AssertMainThread();
	m_iTime = Timer::GetTime(Timer::RESOLUTION_MILLISECONDS);

	BeginPass();
	SetInMirror(false);

	if(!m_bMaterialsSorted)
	{
		Array<Material*> mats;

		for(uint m = 0; m < this->GetMaterials().GetCardinality(); m++)
		{
			Material* mat = m_Materials[m];

			if(mat->GetMaterialClass() == NULL)
			{
				mat->m_iSortID = 0;
				continue;
			}

			while(mat)
			{
				if(mat->GetMaterialClass()->IsAbstract())
				{
					mat->m_iSortID = 0;
				}
				else
				{
					mats.Insert(mat);
				}
				mat = mat->m_pNextClone;
			}
		}
	
		QuickSortCmp<Material*, CompareMaterials>(mats.GetCardinality(), mats.GetArray());
		
		for(uint m = 0; m < mats.GetCardinality(); m++)
		{
			mats[m]->m_iSortID = m + 1;
		}
		m_iMaterialSortVersion++;
		m_bMaterialsSorted = true;
	}

	m_iTimeStamp++;

	#ifdef ENF_DONE
		SetupCamera(viewmatrix, SetupViewMatrix(viewmatrix));
	#else
		Matrix43 invMatrix(MInverseGeneral, viewmatrix);
		SetupCamera(viewmatrix, invMatrix);
	#endif

	SetBias(0.0f);

	if(type <= 	RT_NORMAL_TYPES)
	{
		SET_DSS(DSS_ZENABLE_LE);
		SET_RS( RS_FILLMODE, FILL_SOLID );
	}
	SET_RS( RS_COLORWRITEENABLE, COLORWRITEENABLE_ALL);

	///////
	switch(type)
	{
	case RT_WIREFRAME:
		SET_DSS(DSS_NONE);
		SET_RS( RS_FILLMODE, FILL_WIREFRAME );
		SET_BLEND( 0, false);
		SET_RS( RS_ALPHATESTENABLE, false);
		SET_RS( RS_CULLMODE, CULL_NONE);
		break;

	case RT_INMIRROR:
		SetInMirror(true);
		break;
	}

	if(ENF_DIAG_BOOL(DM_REND_COMPLEX))
	{
		SET_BLEND( 0, false);
		SET_RS( RS_ALPHATESTENABLE, false);
		SET_RS( RS_CULLMODE, CULL_CCW);
		SET_RS( RS_COLORWRITEENABLE, 0);
		SET_RS( RS_FILLMODE, FILL_SOLID );
	}

	SET_RS(RS_DEPTHBIAS, 0);

	for(uint m = 0; m < this->m_MatClasses.GetCardinality(); m++)
	{
		m_MatClasses[m]->BeginFrame();
	}
}

//-----------------------------------------------------------------------
Material* IMaterialSystem::GetMaterial(CStr name)
{
	CMutexLock lock(m_Mutex);
	Material* mat =  m_MaterialsHash.Find(name.cstr());
	if(mat)
		mat->AddRef();

	return mat;
}

//-----------------------------------------------------------------------
bool IMaterialSystem::MaterialExists(CStr matname) const
{
	CMutexLock lock(m_Mutex);
	return m_MaterialsHash.Contains(matname.cstr());
}

//-----------------------------------------------------------------------
Material* IMaterialSystem::CreateMaterial(CStr name)
{
	CMutexLock lock(m_Mutex);

	Material* mat = GetMaterial(name);
	if(mat)
	{
		return mat;
	}
	
	mat = ENF_NEW Material(this, name);

	m_Materials.Insert(mat);
	m_MaterialsHash.Insert(mat->GetName().cstr(), mat);
	m_bMaterialsSorted = false;
	return mat;
}

//-----------------------------------------------------------------------
void IMaterialSystem::RemoveMaterial(Material* material)
{
	CMutexLock lock(m_Mutex);
	if(m_Materials.Remove(material))
	{
		m_MaterialsHash.Remove(material->GetName().cstr());
		//Not necessary, they are sorted anyway
		//m_bMaterialsSorted = false;
	}
}

//-----------------------------------------------------------------------
IMaterialSystem::~IMaterialSystem()
{
	CMutexLock lock(m_Mutex);
	MemoryManager::UnregisterFlushableObject(this);

	uint n;

	uint ntoremove = m_Materials.GetCardinality();
	Material** toremove = ALLOCA(Material*, ntoremove);

	for(n = 0; n < ntoremove; n++)
		toremove[n] = m_Materials[n];

	for(n = 0; n < ntoremove; n++)
		toremove[n]->Release();

	//these leaked
	if(!m_Materials.IsEmpty())
	{
		enf::LogF(LL_ERROR, "==== Material leaks ====");

		for(uint n = 0; n < m_Materials.GetCardinality(); n++)
		{
			for(const Material* mat = m_Materials[n]; mat; mat = mat->m_pNextClone)
			{
				if(mat->IsClone())
					enf::LogF(LL_ERROR, "   %s(%x)   %d", mat->GetName().cstr(), mat->m_iCreationFlags, mat->RefCount());
				else
					enf::LogF(LL_ERROR, "%s(%x)   %d", mat->GetName().cstr(), mat->m_iCreationFlags, mat->RefCount());
			}
		}
	}

	for(n = 0; n < m_Materials.GetCardinality();)
	{
		Material* mat = m_Materials[n];
		if(mat->IsClone())
		{
			delete mat;
		}
		else
			n++;
	}
	for(n = 0; n < m_Materials.GetCardinality();)
	{
		delete m_Materials[n];
	}

	m_Materials.Clear();

	ForEach<DeleteItem>(m_MatClasses);
	ForEach<ReleaseItem>(m_SuperShaders);

	SAFE_RELEASE(m_pIdentityNormalMap);

	for(n = 0 ; n < m_IdentityTextures.GetCardinality(); n++)
	{
		SAFE_RELEASE(m_IdentityTextures[n].m_pTexture);
	}
	m_IdentityTextures.Clear();

	ForEach<SafeReleaseItem>(m_TextureVariables);

	for(uint r = 0; r < m_RenderTargets.GetCardinality(); r++)
	{
		m_RenderTargets[r].m_pRT.Release();
	}
	m_RenderTargets.Clear();
	m_RenderTargetsSet.Clear();

	if(g_pMaterialSystem == this)
		g_pMaterialSystem = NULL;
}

//-----------------------------------------------------------------------
void IMaterialSystem::OnInsert(TiXmlElement* xmlnode)
{
	OnChangeSettings(xmlnode);
}

//-----------------------------------------------------------------------
bool IMaterialSystem::OnChangeSettings(TiXmlElement* xmlnode)
{
	uint texfilter = 0;
	uint maxaniso = 1;
	Renderer* rend = GetEngine()->GetRender();
	float objectLODScale = 1.0f;

	enf_assert2(rend != NULL, "You must have Render module insert before MaterialSystem");

	TiXmlElement* video = xmlnode->FindOrCreateChildElement("video");
	CStr ctexfilter = video->FindOrCreateAttribute("texturefilter", "0");
	texfilter = enf_limit(ctexfilter.ToInt(), 0, 2);
	
	CStr cmaxaniso = video->FindOrCreateAttribute("maxaniso", "1");
	maxaniso = enf_max((uint)cmaxaniso.ToInt(), 1U);
	TiXmlElement* render = xmlnode->FindOrCreateChildElement("render");

	static const float scales[] = {0.4f, 0.7f, 1.0f, 1.5f, 2.0f};
	CStr geomDetail = render->FindOrCreateAttribute("geometricdetail", "2");
	int igeomDetail = enf_limit(geomDetail.ToInt(), 0, 4);
	objectLODScale = scales[igeomDetail];

	SetScalar(MV_OBJECT_LOD_SCALE, objectLODScale);

	RenderTextureFilterState MinLinear = TEXF_LINEAR;
	RenderTextureFilterState MagLinear = TEXF_LINEAR;
	RenderTextureFilterState MipLinear = TEXF_LINEAR;
	RenderTextureFilterState MagAnisotropy = TEXF_ANISOTROPIC;
	RenderTextureFilterState MinAnisotropy = TEXF_ANISOTROPIC;

	//fill the filters
	uint	minFilters[FILTER_MAX_VALUE];
	uint	magFilters[FILTER_MAX_VALUE];
	uint	mipFilters[FILTER_MAX_VALUE];
	uint	maxAniso[FILTER_MAX_VALUE];

	for(uint n = 0; n < FILTER_MAX_VALUE; n++)
	{
		minFilters[n] = MinLinear;
		magFilters[n] = MagLinear;
		mipFilters[n] = TEXF_POINT;
		maxAniso[n] = 1;
	}

	//Set min filters
	minFilters[FILTER_NONE] = TEXF_POINT;
	if(texfilter == 2)
	{
		minFilters[FILTER_DIFFUSEMAP] = MinAnisotropy;
		minFilters[FILTER_NORMALMAP] = MinAnisotropy;
	}

	//Set mag filters
	magFilters[FILTER_NONE] = TEXF_POINT;
	if(texfilter == 2)
	{
		magFilters[FILTER_DIFFUSEMAP] = MagAnisotropy;
		magFilters[FILTER_NORMALMAP] = MagAnisotropy;
		magFilters[FILTER_HIGH] = MagAnisotropy;
		magFilters[FILTER_SUPER_DIFFUSEMAP] = MagAnisotropy;
		magFilters[FILTER_SUPER_NORMALMAP] = MagAnisotropy;
	}

	//Set mip filters
	if(texfilter > 0)
	{
		mipFilters[FILTER_DIFFUSEMAP] = MipLinear;
		mipFilters[FILTER_NORMALMAP] = MipLinear;
		mipFilters[FILTER_TERRAIN_DIFFUSEMAP] = MipLinear;
		mipFilters[FILTER_TERRAIN_NORMALMAP] = MipLinear;
	}

#if defined(ENF_PS3)
	maxAniso[FILTER_DIFFUSEMAP] = 1;
	maxAniso[FILTER_NORMALMAP] = 4;
	maxAniso[FILTER_TERRAIN_NORMALMAP] = 2;
	maxAniso[FILTER_SUPER_DIFFUSEMAP] = 1;
	maxAniso[FILTER_SUPER_NORMALMAP] = 1;
#else
	maxAniso[FILTER_SUPER_DIFFUSEMAP] = enf_max(1U, maxaniso * 2 / 3); //improves performance
	maxAniso[FILTER_DIFFUSEMAP] = enf_max(1U, maxaniso * 2 / 3); //improves performance
	maxAniso[FILTER_NORMALMAP] = maxaniso;
#endif

	for(uint n = 0; n < FILTER_MAX_VALUE; n++)
	{
		m_Filters[n] = g_pRender->CreateSamplerFilterState(SamplerFilterState(minFilters[n], magFilters[n], mipFilters[n], maxAniso[n]));
	}

	for (uint i = 0; i < m_MatClasses.GetCardinality(); i++)
	{
		m_MatClasses[i]->OnChangeSettings(xmlnode);
	}

	return true;
}

//-----------------------------------------------------------------------
void IMaterialSystem::Flush(unsigned int level)
{
	/*
	for(uint n = 0; n < m_CurrentRenderTextures.GetCardinality(); n++)
		m_CurrentRenderTextures[n].m_pTexture.Release();
	m_iLastValidRender = -1;

	for(uint n = 0; n < m_RenderTargetsSet.GetCardinality(); n)
	{
		uint ihandle = m_RenderTargetsSet[n].Handle;
		if(m_RenderTargets[ihandle].m_iRef)
		{
			n++;
			continue;
		}

		m_RenderTargets[ihandle].m_pRT.Release();
		m_RenderTargets.RemoveElementOrdered(ihandle);
		m_RenderTargetsSet.RemoveElement(n);
	}
	*/
}

//-----------------------------------------------------------------------
Texture* IMaterialSystem::GetCurrentRender()
{
	uint w = g_pRender->GetTargetWidth();
	uint h = g_pRender->GetTargetHeight();

	Texture* rt = NULL;
	uint bestindex = -1;

	for(uint n = 0; n < m_CurrentRenderTextures.GetCardinality(); n++)
	{
		Texture* tex = m_CurrentRenderTextures[n].m_pTexture;
		if(tex->GetWidth() == w && tex->GetHeight() == h)
		{
			rt = tex;
			m_CurrentRenderTextures[n].m_iTimeStamp = m_iTime;
			if(n != m_iLastValidRender)
			{
				m_iLastValidRender = -1;
				bestindex = n;
			}
			break;
		}
	}
	if(rt == NULL)
	{
		//better bad one than nothing
		if(m_CurrentRenderTextures.IsFull())
		{
			TimeStamp oldest = 0;
			bestindex = 0;
			for(uint n = 0; n < m_CurrentRenderTextures.GetCardinality(); n++)
			{
				TimeStamp dist = Timer::Sub(m_iTime, m_CurrentRenderTextures[n].m_iTimeStamp);
				if(dist > oldest)
				{
					oldest = dist;
					bestindex = n;
				}
			}
			m_iLastValidRender = -1;
			//reallocate
			m_CurrentRenderTextures[bestindex].m_pTexture.Release();
			rt = g_pRender->CreateTextureWithFormat(NULL, w, h, g_pRender->GetSetupFormat(), TEXTR_RENDERTARGET|TEXTR_NOMIPMAP, MAP_IMAGE);
			m_CurrentRenderTextures[bestindex].m_pTexture = rt;
			m_CurrentRenderTextures[bestindex].m_iTimeStamp = m_iTime;
		}
		else
		{
			rt = g_pRender->CreateTextureWithFormat(NULL, w, h, g_pRender->GetSetupFormat(), TEXTR_RENDERTARGET|TEXTR_NOMIPMAP, MAP_IMAGE);
			RTTCache& item = m_CurrentRenderTextures.Append();
			item.m_pTexture = rt;
			item.m_iTimeStamp = m_iTime;
		}
	}

	if(rt && m_iLastValidRender == -1)
	{
#if defined(ENF_PS3)
		g_pRender->CheckCommandBuffer(256);

		CellGcmSurface rend = g_pRender->GetCurrentSurface();
		
		CellGcmTexture* pOut = rt->GetGCMTexture();

		uint mode = CELL_GCM_TRANSFER_LOCAL_TO_LOCAL;
		if(pOut->location == CELL_GCM_LOCATION_MAIN)
			mode += 2;

		if(rend.colorLocation[0] == CELL_GCM_LOCATION_MAIN)
			mode += 1;

		uint bpp = enf_max(GetFormatStride(rt->GetFormat()), 2);
		bpp = enf_min(bpp, 4);

		//2d transfer
		GCM_CALL(cellGcmSetTransferLocation)(gCellGcmCurrentContext, pOut->location);

		uint ret = GCM_CALL(cellGcmSetTransferImage)(
			gCellGcmCurrentContext, 
			mode,
			pOut->offset, 
			pOut->pitch, 
			0, 0,
			rend.colorOffset[0], 
			rend.colorPitch[0], 
			0, 0,
			pOut->pitch/bpp, 
			rt->GetHeight(), 
			bpp);
	
		enf_assert(ret == CELL_OK);
		GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);

#elif defined(ENF_OGL)
		glBindTexture(GL_TEXTURE_2D, rt->GetGLTexture());
		glCopyTexImage2D(GL_TEXTURE_2D, 0, TexFormatToGLFormat(rt->GetFormat(), true),
			0, 0,
			rt->GetWidth(),
			rt->GetHeight(),
			0
			);
		glBindTexture(GL_TEXTURE_2D, 0);
		g_pRender->DirtyMask(Renderer::GLCM_TEXTURES);
#endif
		m_iLastValidRender = bestindex;
	}
	return rt;
}

//-----------------------------------------------------------------------
ENF_INLINE int IMaterialSystem::FindGE(const Set<IMaterialSystem::RTCacheSort>& set, const IMaterialSystem::RTCacheSort &a)
{
	for(uint n = 0; n < set.GetCardinality(); n++)
	{
		if(set[n].compareNH(a) == 0)
			return n;
	}
	return -1;
/*
	int l = 0;
	int r = set.GetCardinality() - 1;
	int i;
	for(;r >= l;)
	{
		i = (l + r) / 2;
		if(set[i].compareNH(a) > 0)
		{
			r = i - 1;
		}
		else
		{
			if(set[i].compareNH(a) >= 0)
			{
				return i;
			}
			l = i + 1;
		}
	}
	return -1;
*/
}

//-----------------------------------------------------------------------
RenderTarget* IMaterialSystem::AllocRenderTarget(uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, FSAAType fsaa, uint usage, uint edramoffset, uint& handle, uint texFlags /*= 0*/)
{
	RTCacheSort sort;

	sort.Width = width;
	sort.Height = height;
	sort.RTFormat = rtfmt;
	sort.DepthFormat = depthfmt;
	sort.FSAA = fsaa;
	sort.Usage = usage;
	sort.EDRAMOffset = edramoffset;
	sort.Handle = 0;

	int index = FindGE(m_RenderTargetsSet, sort);


	uint ihandle = -1;

	if(index >= 0)
	{
		for(uint n = index; n < m_RenderTargetsSet.GetCardinality(); n++)
		{
			uint ihandle = m_RenderTargetsSet[n].Handle;

			int comp = m_RenderTargetsSet[n].compareNH(sort);

			if(comp > 0)
				break;

			//WTF?
			if(comp < 0)
				continue;

			if(m_RenderTargets[ihandle].m_iRef > 0)
				continue;

			m_RenderTargets[ihandle].m_iTimeStamp = m_iTime;
			m_RenderTargets[ihandle].m_iRef++;
			handle = ihandle + 1;
			return m_RenderTargets[ihandle].m_pRT;
		}
	}

	if(m_RenderTargets.IsFull())
	{
		TimeStamp oldest = 0;
		int bestindex = -1;

		for(uint n = 0; n < m_RenderTargetsSet.GetCardinality(); n++)
		{
			ihandle = m_RenderTargetsSet[n].Handle;
			if(m_RenderTargets[ihandle].m_iRef)
				continue;

			TimeStamp dist = Timer::Sub(m_iTime, m_RenderTargets[ihandle].m_iTimeStamp);
			if(dist > oldest)
			{
				oldest = dist;
				bestindex = n;
			}
		}

		enf_assert(bestindex != -1);

		//reset set
		ihandle = m_RenderTargetsSet[bestindex].Handle;
		m_RenderTargetsSet.RemoveElement(bestindex);

		//reallocate
		m_RenderTargets[ihandle].m_pRT.Release();
		m_RenderTargets[ihandle].m_pRT = g_pRender->CreateRenderTarget(width, height, rtfmt, depthfmt, fsaa, usage, edramoffset, texFlags, "RTCache");
	}
	else
	{
		ihandle = m_RenderTargets.GetCardinality();
		RTCache& item = m_RenderTargets.Append();
		item.m_iRef = 0;
		item.m_pRT = g_pRender->CreateRenderTarget(width, height, rtfmt, depthfmt, fsaa, usage, edramoffset, texFlags, "RTCache");
	}
	
	sort.Handle = ihandle;
	m_RenderTargetsSet.Insert(sort);

	m_RenderTargets[ihandle].m_iTimeStamp = m_iTime;
	m_RenderTargets[ihandle].m_iRef++;
	handle = ihandle + 1;
	return m_RenderTargets[ihandle].m_pRT;
}

//-----------------------------------------------------------------------
void IMaterialSystem::FreeRenderTarget(uint handle)
{
	enf_assert(handle > 0 && handle <= m_RenderTargets.GetCardinality());
	handle--;
	enf_assert(m_RenderTargets[handle].m_iRef > 0);
	m_RenderTargets[handle].m_iRef--;
}

//-----------------------------------------------------------------------
MaterialClass* IMaterialSystem::FindMaterialClass(const char* name)
{
	for(uint n = 0; n < m_MatClasses.GetCardinality(); n++)
	{
		if(m_MatClasses[n]->GetName() == name)
			return m_MatClasses[n];
	}
	return NULL;
}

//-----------------------------------------------------------------------
MaterialClass* IMaterialSystem::FindMaterialClass(const MaterialDescription& mdesc, CStr inherited)
{
	for(uint n = 0; n < m_MatClasses.GetCardinality(); n++)
	{
		CStr classname = m_MatClasses[n]->GetName();

		if(!inherited.IsNull())
		{
			if(!m_MatClasses[n]->HasType(inherited))
				continue;
		}

		if(m_MatClasses[n]->IsCompatible(mdesc))
			return m_MatClasses[n];
	}
	return NULL;
}

//-----------------------------------------------------------------------
bool IMaterialSystem::AddMaterialClass(MaterialClass* mr)
{
	//TODO: we must sort them, that inherited classes are AFTER their ancestors!
	//it's really dangerous, to let it upon the creator to do this!
	CStr name = mr->GetName();

	if(mr->OnInit(GetEngine()->GetSettings()->RootElement()) == EE_OK)
	{
		m_MatClasses.Insert(mr);
		return true;
	}
	
	return false;
}

//-----------------------------------------------------------------------
void IMaterialSystem::OnEvent(const Event& event)
{
	Module::OnEvent(event);

	if(event.HasType(ReloadShadersEventTypeID))
	{
		for(uint n = 0; n < m_MatClasses.GetCardinality(); n++)
		{
			m_MatClasses[n]->OnReloadShaders();
		}
		return;
	}
}

//-----------------------------------------------------------------------
#define PDECL_INTEGER(name, defvalue, uiwidget, rangescale, desc, enums) \
	MaterialVarDef(DVT_INTEGER, 0, 0, name, defvalue, uiwidget, rangescale, desc, enums)

#define PDECL_SCALAR(name, defvalue, uiwidget, rangescale, desc, enums) \
	MaterialVarDef(DVT_SCALAR, 0, 0, name, defvalue, uiwidget, rangescale, desc, enums)

#define PDECL_VECTOR(name, defvalue, uiwidget, desc, enums) \
	MaterialVarDef(DVT_VECTOR, 0, 0, name, defvalue, uiwidget, NULL, desc, enums)

#define PDECL_COLOR(name, defvalue, desc, enums) \
	MaterialVarDef(DVT_COLOR, 0, 0, name, defvalue, "color", NULL, desc, enums)

#define PDECL_TEXTURE(subtype, name, defvalue, desc, enums) \
	MaterialVarDef(DVT_TEXTURE, subtype, 0, name, defvalue, "texture", NULL, desc, enums)

#define PDECL_BOOLEAN(name, defvalue, desc) \
	MaterialVarDef(DVT_BOOLEAN, 0, 0, name, defvalue, "checkbox", NULL, desc)

#define PDECL_STRING(name, defvalue, uiwidget, desc, enums) \
	MaterialVarDef(DVT_STRING, 0, 0, name, defvalue, uiwidget, NULL, desc, enums)

#define PDECL_POINTER(name, desc) \
	MaterialVarDef(DVT_POINTER, 0, 0, name, "0", "none", NULL, desc, NULL)

#define PDECL_FLAGS(name, defvalue, desc, enums) \
	MaterialVarDef(DVT_FLAGS, 0, 0, name, defvalue, "flags", NULL, desc, enums)

//-----------------------------------------------------------------------
uint IMaterialSystem::AddVariableDef(const MaterialVarDef& mvdef)
{
	uint index = m_MaterialVarDefs.GetCardinality();
	m_MaterialVarHash.Insert(mvdef.m_strName, index);
	m_MaterialVarDefs.Insert(mvdef);
	return index;
}

//-----------------------------------------------------------------------
const MaterialVarDef* IMaterialSystem::GetVariableDef(uint index) const
{
	if(index >= m_MaterialVarDefs.GetCardinality())
		return NULL;

	return &m_MaterialVarDefs[index];
}

//-----------------------------------------------------------------------
const MaterialVarDef* IMaterialSystem::GetVariableDef(CStr name) const
{
	uint index = FindVariableDef(name);

	if(index == INDEX_NOT_FOUND)
		return NULL;

	return &m_MaterialVarDefs[index];
}

//-----------------------------------------------------------------------
uint IMaterialSystem::GetNumVariableDefs() const
{
	return m_MaterialVarDefs.GetCardinality();
}

//-----------------------------------------------------------------------
uint IMaterialSystem::FindVariableDef(CStr name) const
{
	uint index;
	if(m_MaterialVarHash.Find(name, index))
		return index;

	return INDEX_NOT_FOUND;
}

//-----------------------------------------------------------------------
IMaterialSystem::IMaterialSystem(Renderer* device, ScriptContext* script, bool editormode):
	m_pDevice(device),
	m_pScript(script),
	m_bEditorMode(editormode),
	m_pIdentityNormalMap(NULL),
	m_pIdentityNormalMapDXT(NULL),
	m_pIdentityWhiteMap(NULL),
	m_pIdentityBlackMap(NULL),
	m_bInMirror(false),
	m_fLastBias(-1.0f),
	m_iProjStackPos(0),
	m_iCameraStackPos(0),
	m_bMaterialsSorted(false),
	m_iTime(0),
	m_iTimeStamp(0)
{
	MemoryManager::RegisterFlushableObject(this);

	ZeroMem(m_Filters, sizeof(m_Filters));
	ZeroMem(Material::m_pEmptyVariables, sizeof(Material::m_pEmptyVariables));

	m_pRenderViewName = g_pStatic->AddString("$renderview");
	m_pGUIName[0] = g_pStatic->AddString("$rendertarget");
	m_pGUIName[1] = g_pStatic->AddString("$rendertarget2");
	m_pGUIName[2] = g_pStatic->AddString("$rendertarget3");
	m_pGUIName[3] = g_pStatic->AddString("$rendertarget4");
	m_pNearestCubeName = g_pStatic->AddString("$nearestcube");

	//built-in variables. Must be added as the first and their
	//order must match MV_ constants in enf_materialsystem.h!!!!!
	AddVariable("MV_PROJECTVIEW_MATRIX", DVT_MATRIX);
	AddVariable("MV_PROJECT_MATRIX", DVT_MATRIX);
	AddVariable("MV_PROJECTTRANS_MATRIX", DVT_MATRIX);
	AddVariable("MV_VIEW_MATRIX", DVT_MATRIX);
	AddVariable("MV_VIEWTRANS_MATRIX", DVT_MATRIX34);
	AddVariable("MV_BILLBOARD_MATRIX", DVT_MATRIX);
	AddVariable("MV_EYE_POS", DVT_VECTOR);
	AddVariable("MV_ASPECT", DVT_SCALAR);
	AddVariable("MV_NEARPLANE", DVT_SCALAR);
	AddVariable("MV_FARPLANE", DVT_SCALAR);
	AddVariable("MV_IDENTNORMAL_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_IDENTNORMALDXT_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_BLACK_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_WHITE_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_GRAY_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_DEFAULT_SKY_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_WHITE_CUBE_TEXTURE", DVT_TEXTURE);
	AddVariable("MV_TIME", DVT_SCALAR);
	AddVariable("MV_SHADOWVIEWPROJ_TRANS_MAT", DVT_MATRIX34, 2);
	AddVariable("MV_SHADOWVIEWPROJ_TERRAIN_TRANS_MAT", DVT_MATRIX34, 2);
	AddVariable("MV_SHADOW_MIN_DISTANCE", DVT_SCALAR);
	AddVariable("MV_SHADOW_MAX_DISTANCE", DVT_SCALAR);
	SetScalar(MV_SHADOW_MIN_DISTANCE, 3500);
	SetScalar(MV_SHADOW_MAX_DISTANCE, 6200);
	AddVariable("MV_SHADOWMAP_PARAMS", DVT_VECTOR);
	{
		//normalized to 4096, which is in shadow shader the 1 value
		float k, q;
		GetLineEq(GetScalar(MV_SHADOW_MAX_DISTANCE)/4096.0f, GetScalar(MV_SHADOW_MIN_DISTANCE)/4096.0f, k, q);
		SetVector(MV_SHADOWMAP_PARAMS, Vector4(k, q, 0.0f, 0.0f));
	}

	AddVariable("MV_CAMERA_MATRIX", DVT_MATRIX);
	AddVariable("MV_INVVIEW_MATRIX", DVT_MATRIX);
	AddVariable("MV_INVVIEWTRANS_MATRIX", DVT_MATRIX);
	AddVariable("MV_DEPTH_RANGE", DVT_SCALAR);
	AddVariable("MV_DEPTH_PARAMS", DVT_VECTOR);
	
	AddVariable("MV_TERRAIN_SHADOW", DVT_VECTOR);
	SetVector(MV_TERRAIN_SHADOW, Vector4(0.0f, 0.0f, 0.0f, 0.0f));

	AddVariable("MV_TERRAIN_SHADOW_MAP", DVT_TEXTURE);
	#ifdef ENF_PS3
		AddVariable("MV_TERRAIN_SHADOW_MAP_8BIT", DVT_TEXTURE);
	#endif
	AddVariable("MV_OBJECT_LOD_SCALE", DVT_SCALAR);
	SetScalar(MV_OBJECT_LOD_SCALE, 1.0f);

	SetTexture(MV_IDENTNORMAL_TEXTURE, GetIdentityColorMap(FColor(0.5f, 0.5f, 1.0f, 1.0f), 0));
	SetTexture(MV_IDENTNORMALDXT_TEXTURE, GetIdentityColorMap(FColor(0.0f, 0.5f, 0.0f, 0.5f), 0));
	SetTexture(MV_WHITE_TEXTURE, GetIdentityColorMap(FColor(1.0f, 1.0f, 1.0f, 1.0f), 0));
	SetTexture(MV_BLACK_TEXTURE, GetIdentityColorMap(FColor(0.0f, 0.0f, 0.0f, 1.0f), 0));
	SetTexture(MV_GRAY_TEXTURE, GetIdentityColorMap(FColor(0.5f, 0.5f, 0.5f, 1.0f), 0));
	SetTexture(MV_DEFAULT_SKY_TEXTURE, GetIdentityColorMap(FColor(0.13f, 0.64f, 0.7f, 0.0f), TEXTR_CUBE));
	SetTexture(MV_WHITE_CUBE_TEXTURE, GetIdentityColorMap(FColor(1.0f, 1.0f, 1.0f, 1.0f), TEXTR_CUBE));
	#ifdef ENF_PS3
		SetTexture(MV_TERRAIN_SHADOW_MAP, GetTexture(MV_BLACK_FLOAT_TEXTURE));
		SetTexture(MV_TERRAIN_SHADOW_MAP_8BIT, GetTexture(MV_BLACK_TEXTURE));
	#else
		SetTexture(MV_TERRAIN_SHADOW_MAP, GetTexture(MV_BLACK_TEXTURE));
	#endif
	
	//built-in common material parameters
	static const ParmEnumInteger sortnamesX[] =
	{
		ParmEnumInteger("opaque", SORT_OPAQUE, "non-translucent geometry, or alpha-masked"),
		ParmEnumInteger("translucent", SORT_TRANSLUCENT, "translucent geometry, water, glass etc."),
		ParmEnumInteger("decal", SORT_DECAL, "Dynamic decals"),
		ParmEnumInteger("mark", SORT_MARK, "Static decals, scratches, signs, writings, blood marks etc"),
		ParmEnumInteger("near", SORT_NEAR, "near camera effects. Bloodspurts"),
		ParmEnumInteger("overlay", SORT_OVERLAY, "sorted behind framebuffer copy. Used for refract effects that need current view"),
		ParmEnumInteger("overall", SORT_OVERALL, "sorted behind all, and after zbuffer clear. Used for firstperson weapons etc.")
	};
	static const ParmEnum* sortnames[] =
	{
		&sortnamesX[0], &sortnamesX[1], &sortnamesX[2], &sortnamesX[3], &sortnamesX[4], 
		&sortnamesX[5], &sortnamesX[6], NULL 
	};

	static const ParmEnumInteger cullmodesX[] =
	{
		ParmEnumInteger("ccw", 0, "Visible only from one side. Default"),
		ParmEnumInteger("none", 1, "Visible from both sides")
	};
	static const ParmEnum* cullmodes[] =
	{
		&cullmodesX[0], &cullmodesX[1], NULL 
	};

	static const ParmEnumInteger surfparmsX[] =
	{
		ParmEnumInteger("solid", 0),
		ParmEnumInteger("nonsolid", SURF_NOSOLID),
		ParmEnumInteger("nodamage", 0),
		ParmEnumInteger("nocastshadow", 0),
		ParmEnumInteger("areaportal", 0),
		ParmEnumInteger("nodraw", SURF_NODRAW),
		ParmEnumInteger("noimpact", SURF_NOIMPACT, "geometry do not make any impact sounds or effects"),
		ParmEnumInteger("nomarks", SURF_NOMARKS, "geometry will not receive any decals nor visible damage"),
		ParmEnumInteger("dust", SURF_DUST, "geometry will be dusty when collides")
	};
	static const ParmEnum* surfparms[] =
	{
		&surfparmsX[0], &surfparmsX[1], &surfparmsX[2], &surfparmsX[3], &surfparmsX[4], 
		&surfparmsX[5], &surfparmsX[6], &surfparmsX[7], &surfparmsX[8], 
		NULL 
	};
	
	static ParmEnumInteger physmaterialsX[256];
	static ParmEnum* physmaterials[256];

	{
		//fill array with base materials
	#ifdef ENF_DONE
		const Array<Variable*> vars = g_pScript->GetGlobalVariables();

		uint nummats = 0;
		for(uint n = 0; n < vars.GetCardinality(); n++)
		{
			if(vars[n]->GetVType() != VARTYPE_INT)
				continue;
		
			int matindex = vars[n]->Int;

			if(matindex < 0 || matindex >= 256)
				continue;
			
			//check our namespace
			if(strncmp(vars[n]->GetName(), "MATERIAL_", 9) != 0)
				continue;

			SString<> name;
			name = (vars[n]->GetName() + 9);
			name.lower();

			physmaterialsX[nummats].Name = g_pStatic->AddString(name.cstr());
			physmaterialsX[nummats].Description = physmaterialsX[nummats].Name;
			physmaterialsX[nummats].Value = matindex;
			physmaterials[nummats] = &physmaterialsX[nummats];
			nummats++;
		}
		physmaterials[nummats] = NULL;
	#else
		physmaterials[0] = NULL;
	#endif
	}

	static const ParmEnumInteger passmasksX[] = 
	{
		ParmEnumInteger("noambient", BITF(PT_AMBIENT), "switches off ambient lightning and is full-bright as original texture"),
		ParmEnumInteger("nolight", BITF(PT_DIFFUSE_SPECULAR), "switches off lights."),
		ParmEnumInteger("noenvironment", BITF(PT_ENVIRONMENT), "switches off environmental reflections")
	};
	static const ParmEnum* passmasks[] = 
	{
		&passmasksX[0], &passmasksX[1], &passmasksX[2], &passmasksX[3], NULL
	};

	AddVariableDef(PDECL_INTEGER("cull", "ccw", "combobox", NULL, "Enables doubleside material", cullmodes));
	AddVariableDef(PDECL_INTEGER("sort", "opaque", "combobox", NULL, "Defines sorting group", sortnames));
	AddVariableDef(PDECL_INTEGER("sortbias", "0", "slider", "-16 15 1", "Fine defines sorting bias", NULL));
	AddVariableDef(PDECL_BOOLEAN("castshadow", "false", "Casts shadows"));
	AddVariableDef(PDECL_BOOLEAN("receiveshadow", "false", "Receive shadow"));
	AddVariableDef(PDECL_SCALAR("distance", "-1", "editbox", NULL, "Distance until material is not rendered. Default is -1, infinity.", NULL));
	AddVariableDef(PDECL_FLAGS("surfparm", "", "Surface parameters", surfparms));
	AddVariableDef(PDECL_INTEGER("physmaterial", "default", "combobox", NULL, "Physical material. Surfaces with certain material sounds and behave like:", (const ParmEnum**)physmaterials));
	AddVariableDef(PDECL_FLAGS("passmask", "", "Switches on/off render passes", passmasks));
	AddVariableDef(PDECL_BOOLEAN("mirror", "false", "Surface is mirroring"));

	static const ParmEnumMaterialDeform deformfuncsX[] = 
	{
		ParmEnumMaterialDeform("tcmod scale", Material::TCMOD_SCALE, "scales U,V around center (CenterU, CenterV, ScaleU, ScaleV)", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("tcmod shift", Material::TCMOD_SHIFT, "shifts U,V (OffsetU, OffsetV)", DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("tcmod rotate", Material::TCMOD_ROTATE, "rotates U,V (Angle, CenterU, CenterV)", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("tcmod scale2", Material::TCMOD_SCALE2, "scales U,V around center (CenterU, CenterV, ScaleU, ScaleV)", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("tcmod shift2", Material::TCMOD_SHIFT2, "shifts U,V (OffsetU, OffsetV)", DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("tcmod rotate2", Material::TCMOD_ROTATE2, "rotates U,V (Angle, CenterU, CenterV)", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR),

		ParmEnumMaterialDeform("vcmod scale", Material::VCMOD_SCALE, "moves vertex on normal axis", DVT_SCALAR),
		ParmEnumMaterialDeform("vcmod sin", Material::VCMOD_SIN, "waves with mesh in U,V plane", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR, DVT_SCALAR),
		ParmEnumMaterialDeform("vcmod xysin", Material::VCMOD_XYSIN, "waves with mesh in X,Y plane", DVT_SCALAR, DVT_SCALAR, DVT_SCALAR, DVT_SCALAR)
	};

	static const ParmEnumMaterialDeform* deformfuncs[] =
	{
		&deformfuncsX[0], &deformfuncsX[1], &deformfuncsX[2], &deformfuncsX[3], &deformfuncsX[4],
		&deformfuncsX[5], &deformfuncsX[6], &deformfuncsX[7], &deformfuncsX[8], NULL
	};

	m_pDeformFuncs = deformfuncs;
}

}

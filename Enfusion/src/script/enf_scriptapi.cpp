//-----------------------------------------------------------------------------
// File: enf_scriptapi.cpp
//
// Desc: Script-engine API
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "engine/enf_engine.h"

/*
#include "bspworld/enf_bspworld.h"
#include "baseworld/enf_entity.h"
#include "baseworld/enf_baseworldtrace.h"
#include "baseworld/enf_decals.h"
#include "baseworld/enf_occlusionquery.h"
#include "baseworld/enf_entitydata.h"
#include "baseworld/enf_skydomerender.h"
#include "baseworld/enf_worldmanager.h"
#include "BaseWorld/enf_skypresetmanager.h"
#include "BaseWorld/enf_skydomelayers.h"
*/
#include "script/enf_scriptapi.h"
#include "script/enf_scriptapivars.h"
#include "script/enf_scriptmain.h"
#include "script/enf_scriptcompiler.h"
#include "script/enf_scriptthread.h"
#include "script/enf_scriptdebuginfo.h"
#include "common/enf_streams.h"
#include "common/enf_winding.h"
#include "common/enf_staticbuffer.h"

#include "BaseWorld/enf_baseworldtrace.h"

/*
#include "vobject/meshobject/enf_meshobject.h"
#include "vobject/enf_mmodel.h"
#include "vobject/enf_particleeffect.h"
#include "enf_eparser.h"
#include "enf_shapes.h"
#include "enf_materialsystem.h"
#include "physics/enf_physicsbullet.h"
#include "enf_input.h"
#include "enf_resourcemanager.h"
#include "enf_vfilesystem.h"
#include "enf_profiler.h"
#include "postprocess/enf_postprocesseffects.h"
#include "common/enf_intersection.h"
#include "vobject/enf_animset.h"
#include "enf_animationsystem.h"
#include "common/enf_diag.h"
#include "widgets/enf_uiwidgets.h"
#include "widgets/enf_widgets.h"
#include "widgets/enf_widgetscript.h"
#include "sound/enf_xsoundsystem.h"
#include "script/enf_scriptvariable.h"
#include "enf_addon.h"
#include "Network/enf_netserialize.h"
*/

namespace enf
{
extern bool g_bGrabVideo;

Variable *g_pvFTDelta;
Variable *g_pvLMB;
Variable *g_pvRMB;
Variable *g_pvMMB;
Variable *g_pvMouseDeltaX;
Variable *g_pvMouseDeltaY;
Variable *g_pvMouseWheel;

Variable *g_pvJoyAxis;
Variable *g_pvJoyRAxis;
Variable *g_pvJoySlider;
Variable *g_pvJoyButtons;
Variable *g_pvJoyPOV;
Variable *g_pvWidth;
Variable *g_pvHeight;

Variable *g_pvViewFrame;
Variable *g_pvIsOutdoor;

//Trace additional output
Variable *g_pvTraceShader;
Variable *g_pvTraceContent;
Variable *g_pvTraceSurfparm;
Variable *g_pvTraceBone;


//stats
Variable *g_pvStatFPS;

VarType  *g_vtVObjectType;
VarType  *g_vtVSoundType;
VarType  *g_vtSoundType;

//Render switches
Variable*	g_pvRenderNormal;
Variable*	g_pvRenderParallax;
Variable*	g_pvRenderRTWidgets;
Variable*	g_pvRenderShape;

Variable*	g_pvMaxPhysIterations;
Variable*	g_pvPhysFixedTick;

enum MouseKey
{
	MS_NONE,
	MS_LB,
	MS_MB,
	MS_RB,
	MS_X,
	MS_Y
};


//-----------------------------------------------------------------------
void EntityClass::Delete(Instance* inst) const
{
#ifdef ENF_DONE
	Entity* ent = (Entity*)inst;

	if(ent->IsFlagSet(TFL_REMOVE))
		Module->GetContext()->Error(EnforceScriptContext::RTE_DeletedEntityPointerError);

	Module->GetContext()->CallDestructors(this, inst);
	ent->PrepareForDelete();
#endif
}

//-----------------------------------------------------------------------
void EntityClass::Initialize(Instance* inst, const void *extra) const
{
#ifdef ENF_DONE
	new ((void*)(inst)) Entity((const EntitySource*)extra);
	
	Entity* ent = (Entity*)inst;

	if(BaseWorld::Current())
		BaseWorld::Current()->AddEntity(ent);

	if(extra)
	{
		const EntitySource* src = (const EntitySource*)extra;

		if(!src->GetName().IsEmpty())
		{
			uint vindex  = ent->GetClass()->GetVariableIndex("_name");
			if(vindex != INDEX_NOT_FOUND)
			{
				Module->GetContext()->CopyString(src->GetName().cstr(), ent->GetClass()->GetVariable(vindex), &ent->GetIVariable(vindex)->String);
			}
		}
	}

	if(BaseWorld::Current() && CStr(ent->GetTypeName()) == WORLD_ENTITY_NAME)
	{
		BaseWorld::Current()->SetWorldEntity(ent);
	}
#endif
}

//-----------------------------------------------------------------------
Entity *Parm2Entity(IVariable *var)
{
Entity *en;

	if((en = static_cast<Entity*>(var->InstancePtr)) == NULL)
		g_pScript->Error(ScriptContext::RTE_NullPointerError);

	if(en->IsFlagSet(TFL_REMOVE))
		g_pScript->Error(EnforceScriptContext::RTE_DeletedEntityPointerError);

	return en;
}

//-----------------------------------------------------------------------
Entity *AssertEntity(Instance* inst)
{
	Entity* en = static_cast<Entity*>(inst);

	if(en == NULL)
		g_pScript->Error(ScriptContext::RTE_NullPointerError);

	if(en->IsFlagSet(TFL_REMOVE))
		g_pScript->Error(EnforceScriptContext::RTE_DeletedEntityPointerError);

	return en;
}

//-----------------------------------------------------------------------
#ifdef ENF_DONE
void BaseWorld::AddThread(Instance* ins, ScriptThread *rt)
{
#ifdef _DEBUG
		{
			ScriptThread* th = ins ? ins->m_psThread : m_psThread;
			while(th)
			{
				enf_assert(rt != th);
				th = th->m_pUserNext;
			}
		}
#endif


	if(ins != NULL)
	{
		//add to list of owner
		ScriptThread **prev = &ins->m_psThread;

		rt->m_pUserNext = *prev;
		if(*prev)
			(*prev)->m_pUserPrev = &rt->m_pUserNext;
		
		*prev = rt;
		rt->m_pUserPrev = prev;
	}
	else
	{
		rt->m_pUserNext = m_psThread;
		if(m_psThread)
			m_psThread->m_pUserPrev = &rt->m_pUserNext;
		
		m_psThread = rt;
		rt->m_pUserPrev = &m_psThread;
	}

	rt->m_iID = reinterpret_cast<intptr_t>(this);
}

//-----------------------------------------------------------------------
Widget *AssertWidget(Widget* w)
{
	if(w == NULL)
		g_pScript->Error("NULL pointer to widget");

	return w;
}
#endif

#ifdef ENF_DONE

namespace API
{
//----------------------------------------------------------------------
//proto void DumpStack()
//----------------------------------------------------------------------
SCAPI(DumpStack)
{
	ScriptThread* st = ScriptContext::m_pCurrent->GetCurrThread();

	enf::LogF(LL_ERROR, "   -- Stack trace --");
	for(uint s = st->GetStackPos(); s >= 1; s--)
	{
		const char *buff;
		uint line;

		const VMStack* stack = st->GetStack(s);
		const DebugInfo* dinfo = stack->m_pfFunc->GetModule()->GetDebugInfo();

		if(dinfo && dinfo->GetLineInfoByOffset((ubyte*)stack->m_psStat - (ubyte*)stack->m_pfFunc->GetModule()->GetStaticBuffer()->GetData(), &buff, &line))
		{
			enf::LogF(LL_ERROR, "   %s() %s : %d", st->GetStack(s)->m_pfFunc->GetName(), buff, line);
		}
	}
	enf::LogF(LL_ERROR, "   -----------------");
}

//-----------------------------------------------------------------------
SCAPI(AddNamedEntity)
{
	Entity* ent = (Entity*)parm[0]->InstancePtr;
	const char* name = parm[1]->String;

	enf_assert(ent->GetWorld() != NULL);
	ent->GetWorld()->InsertNamedEntity(name, ent);
}

//-----------------------------------------------------------------------
void NMETHOD RemoveNamedEntity(Entity* ent, const char* name)
{
	ent->GetWorld()->RemoveNamedEntity(name, ent);
}

//-----------------------------------------------------------------------
Entity* NMETHOD FindNamedEntity(Entity* worldEnt, const char* name)
{
	AssertEntity(worldEnt);
	if(!worldEnt->GetWorld())
		return NULL;
	return worldEnt->GetWorld()->FindNamedEntity(name);
}

//-----------------------------------------------------------------------
Entity* NMETHOD FindEntityByID(Entity* worldEnt, uint ID)
{
	AssertEntity(worldEnt);
	if(!worldEnt->GetWorld())
		return NULL;
	return worldEnt->GetWorld()->FindEntityByID(ID);
}

//-----------------------------------------------------------------------
// proto native int CreateOcclusionQuery(vector pos, float size)
//-----------------------------------------------------------------------
OcclusionQuery* NMETHOD CreateOcclusionQuery(const Vector3& pos, float size)
{
	return BaseWorld::Current()->CreateOcclusionQuery(pos, size);
}

//-----------------------------------------------------------------------
// proto int		AddDShape(int type, int color, int flags, vector p1, vector p2)
//-----------------------------------------------------------------------
Shape* NMETHOD AddDShape(ShapeType type, uint color, uint flags, const Vector3& p1, const Vector3& p2)
{
	switch(type)
	{
	case SHAPE_LINE:
		return Shape::CreateLine(p1, p2, color, flags);
	case SHAPE_BBOX:
		return Shape::CreateBox(p1, p2, color, flags);
	case SHAPE_DIAMOND:
		return Shape::CreateDiamond(p1, p2, color, flags);
	case SHAPE_PYRAMID:
		return Shape::CreatePyramid(p1, p2, color, flags);
	case SHAPE_SPHERE:
		return Shape::CreateSphere((p1 + p2) * 0.5f, AAB(p1, p2).GetDiameter() * 0.5f, color, flags);
	case SHAPE_CYLINDER:
		return Shape::CreateCylinder((p1 + p2) * 0.5f, AAB(p1, p2).GetSize().x * 0.5f, AAB(p1, p2).GetSize().z * 0.5f, color, flags);
	}
	return NULL;
}

//-----------------------------------------------------------------------
// proto int		AddDLines(int color, int flags, vector p[], int num)
//-----------------------------------------------------------------------
Shape* NMETHOD AddDLines(uint color, uint flags, const Vector3* p, uint num)
{
	Shape *sh = Shape::CreateLines(p, num, color, flags);
	return sh;
}

//-----------------------------------------------------------------------
// proto int		AddDTris(int color, int flags, vector p[], int num)
//-----------------------------------------------------------------------
Shape* NMETHOD AddDTris(uint color, uint flags, const Vector3* p, uint num)
{
	Shape *sh = Shape::CreateMesh(p, num, color, flags);
	return sh;
}

//-----------------------------------------------------------------------
// proto int		AddDSphere(int color, int flags, vector origin, float radius)
//-----------------------------------------------------------------------
Shape* NMETHOD AddDSphere(uint color, uint flags, const Vector3& origin, float radius)
{
	Shape *sh = Shape::CreateSphere(origin, radius, color, flags);
	return sh;
}

//-----------------------------------------------------------------------
// proto int		AddDCylinder(int color, int flags, vector origin, float radius, float length)
//-----------------------------------------------------------------------
Shape* NMETHOD AddDCylinder(uint color, uint flags, const Vector3& origin, float radius, float length)
{
	Shape *sh = Shape::CreateCylinder(origin, radius, length, color, flags);
	return sh;
}

//-----------------------------------------------------------------------
//   proto void		Throw(class ent, class actor, int event, int extra)
//-----------------------------------------------------------------------
SCAPI(Throw)
{
Entity *ent = Parm2Entity(parm[0]);
Entity *act = Parm2Entity(parm[1]);

	ent->ThrowEvent(act, parm[2]->Int, parm[3]->Pointer);
}

//-----------------------------------------------------------------------
// proto tparse	BeginParse(string filename)
//-----------------------------------------------------------------------
EParser* NMETHOD BeginParse(const char *fname)
{
EParser* tp = ENF_NEW EParser;

	if(tp->Parse(fname) != PE_OK)
	{
		delete tp;
		tp = NULL;
	}

	return tp;
}

//-----------------------------------------------------------------------
// proto int		ParseLine(tparse tp, int num, string tokens[])
//-----------------------------------------------------------------------
SCAPI(ParseLine)
{
CParser* tp;

	tp = (CParser*)parm[0]->Pointer;
	if(parm[1]->Int >= 0 && parm[1]->Int < (int)tp->GetNumLines())
	{
		const TLine& tl = tp->BeginLine(parm[1]->Int);
		char **str = (char **)parm[2]->Pointer;
		int maxsize = g_pScript->GetParmDef(2)->ArraySize();

		for(int n = 0; n < tl.m_iNumTokens && n < maxsize; n++)
		{
			g_pScript->CopyString(tp->GetToken(n)->Text, (char**)&str[n], true);
		}

		result->Int = tl.m_iNumTokens;
	}
	else
		result->Int = 0;

}

//-----------------------------------------------------------------------
// proto void		EndParse(tparse file)
//-----------------------------------------------------------------------
void NMETHOD EndParse(EParser *tp)
{
	SAFE_DELETE(tp);
}

//-----------------------------------------------------------------------
//	proto string		vtoa(vobject obj)
//-----------------------------------------------------------------------
SCAPI(vtoa)
{
	g_pScript->CopyString(((VObjectResource*)parm[0]->Pointer)->GetName().cstr(), &result->String, true);
}

class SphereEntityIterator: public EntityIterator
{
public:
	uint FMask;
	int MaxEnt;
	Entity** Entities;
	int NumEnts;

	bool FilterEntity(const Entity* ent)
	{
		return ((ent->GetFlags() & FMask) == FMask);
	}

	bool AddEntity(Entity* ent)
	{
		if(NumEnts >= MaxEnt)
			return false;
		Entities[NumEnts++] = ent;
		return true;
	}

	SphereEntityIterator(uint fmask, int maxent, Entity** ents):
		FMask(fmask),
		MaxEnt(maxent),
		Entities(ents),
		NumEnts(0)
	{
	}
};

//-----------------------------------------------------------------------
// proto int		SphereQuery(vector origin, float radius, class visents[], int ents, int fmask)
//-----------------------------------------------------------------------
SCAPI(SphereQuery)
{

	SphereEntityIterator iter(parm[4]->Int, parm[3]->Int, (Entity **)parm[2]->Pointer);
	BaseWorld::Current()->QueryEntitiesBySphere(NULL, *(Vector3*)parm[0]->Pointer, parm[1]->Float, &iter);
	result->Int = iter.NumEnts;
}

//-----------------------------------------------------------------------
// proto int		VisEntities(vector origin, vector look, float angle, float radius, class visents[], int ents, int fmask)
//-----------------------------------------------------------------------
SCAPI(VisEntities)
{
	result->Int = World::Current()->VisEntities(
		(float *)parm[0]->Pointer,
		(float *)parm[1]->Pointer,
		parm[2]->Float,
		parm[3]->Float,
		(Entity **)parm[4]->Pointer,
		parm[5]->Int,
		parm[6]->Int);
}

//-----------------------------------------------------------------------
// proto bool	IsBoxVisible(vector mins, vector maxs)
//-----------------------------------------------------------------------
int NMETHOD IsBoxVisible(const Vector3& mins, const Vector3& maxs, uint flags)
{
	return 1;
	//return BaseWorld::Current()->IsBoxVisible(AAB(mins, maxs), flags);
}

//-----------------------------------------------------------------------
// proto int   	P2PVisibilityEx(vector from, vector to, int flags)
//-----------------------------------------------------------------------
int NMETHOD P2PVisibilityEx(const Vector3& from, const Vector3& to, uint flags)
{
	return World::Current()->P2PTest(from, to, flags);
}

//-----------------------------------------------------------------------
//proto bool 	TraceLineToEntity(class ent, vector start, vector end, TraceContact contact);
//-----------------------------------------------------------------------
uint NMETHOD TraceLineToEntity(Entity* ent, const Vector3& from, const Vector3& to, TraceContact* contact)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL)
		return false;

	if(ent->GetVObject()->TraceLine(ent, from, to, contact))
	{

		return true;
	}
	return false;
}

struct MTraceScriptCallback: public MTrace
{
	ScriptModule*	pModule;
	Instance**	pInstance;
	Funct*		pFunct;
	uint			iFunct;
	
	ebool FilterEntity(Entity* ent)
	{
		if(iFunct == 0)
			return true;

		if(pFunct->GetNumParameters() >= 1 && pFunct->GetVariable(0)->GetVType() == VARTYPE_CLASS)
			pFunct->GetVariable(0)->InstancePtr = ent;

		if(pFunct->GetNumParameters() >= 2 && pFunct->GetVariable(1)->GetVType() == VARTYPE_VECTOR)
			*(Vector3*)pFunct->GetVariable(1)->Pointer = Start;

		if(pFunct->GetNumParameters() >= 3 && pFunct->GetVariable(2)->GetVType() == VARTYPE_VECTOR)
			*(Vector3*)pFunct->GetVariable(2)->Pointer = Dir;
		
		pModule->Execute(g_pScript->GetCurrThread(), iFunct, pInstance);

		return (pFunct->GetReturnVariable()->Int != 0);
	}

	void SetCallback(Instance** inst, uint func)
	{
		pInstance = inst;
		iFunct = func;

		if(pInstance)
		{
			Instance* inst = *pInstance;

			if(inst == NULL)
				g_pScript->Error("NullPointerError");

			pFunct = inst->GetClass()->GetMethod(iFunct - 1);
		}
		else
		{
			pFunct = pModule->GetFunction(iFunct - 1);
		}
	}

	MTraceScriptCallback(ScriptModule* mod):
		MTrace(),
		pModule(mod),
		pInstance(NULL),
		pFunct(NULL),
		iFunct(0)
	{
	}
};
//-----------------------------------------------------------------------
//proto float 	TraceLineEx(vector start,vector end, vector mins, vector maxs, class cent, float plane[4], int content, int surfparm, int flags, void filtercallback);
//-----------------------------------------------------------------------
SCAPI(TraceLineEx)
{
Vector3 end;
MTraceScriptCallback tr(g_pScript);

	tr.Init(NULL, parm[8]->Int, TFL_SOLID);

	if(parm[9]->FuncPtr != NULL)
	{
		tr.SetCallback(parm[10]->FuncPtr->pInstance, parm[10]->FuncPtr->FuncIndex);
	}

	tr.BBox.Init(*(Vector3*)parm[2]->Pointer, *(Vector3*)parm[3]->Pointer);

	end = (float *)parm[1]->Pointer;

	bool endpos   = ((g_pScript->GetParmDef(1)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool clipent  = ((g_pScript->GetParmDef(4)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool plane    = ((g_pScript->GetParmDef(5)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool contents = ((g_pScript->GetParmDef(6)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool surfparm = ((g_pScript->GetParmDef(7)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);

	World::Current()->TraceMove(NULL,
										*(Vector3*)parm[0]->Pointer,
										end,
										&tr);

	//HACK for editor to place entities on the ground. TODO: real sweep test of box to terrain
	/*if(tr.Terrain && !tr.BBox.GetSize().IsZero() && tr.Dir.z < -0.99f)
	{
		tr.EndPos.z -= tr.BBox.GetMins().z;
		tr.Fraction = tr.Start.Distance(tr.EndPos) / tr.Len;
	}*/

	if(endpos)
	{
		*(Vector3 *)parm[1]->Pointer = tr.EndPos;
	}

	if(clipent)
		parm[4]->Pointer = (void *)tr.ClipEntity;

	if(plane)
	{
		*(Vector4 *)parm[5]->Pointer = tr.Plane;
	}

	if(contents)
		parm[6]->Int = tr.Contents;

	if(surfparm)
		parm[7]->Int = tr.SurfParms.GetParms();

	result->Float = tr.Fraction;

}

//-----------------------------------------------------------------------
//proto float 	TraceLine(class ent,vector start,vector end, class cent, vector norm, int content, int surfparm)
//-----------------------------------------------------------------------
SCAPI(TraceLine)
{
Vector3 end;
Entity *en;
MTrace tr;

	tr.Init((Entity*)parm[0]->InstancePtr, TRACE_ENTS|TRACE_WORLD, TFL_SOLID);

	if((en = (Entity*)parm[0]->InstancePtr) != NULL)
	{
		tr.SetBBox(en);
	}

	end = (float *)parm[2]->Pointer;

	//it's safe to keep it here
	bool endpos   = ((g_pScript->GetParmDef(2)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool clipent  = ((g_pScript->GetParmDef(3)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool plane    = ((g_pScript->GetParmDef(4)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool contents = ((g_pScript->GetParmDef(5)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);
	bool surfparm = ((g_pScript->GetParmDef(6)->Spec & (VSPEC_CONST|VSPEC_PROTECT)) == 0);

	World::Current()->TraceMove(NULL,
										*(Vector3*)parm[1]->Pointer,
										*(Vector3*)parm[2]->Pointer,
										&tr);

	if(endpos)
		*(Vector3 *)parm[2]->Pointer = tr.EndPos;

	if(clipent)
		parm[3]->Pointer = (void *)tr.ClipEntity;

	if(plane)
	{
		*(Vector4 *)parm[4]->Pointer = tr.Plane;
	}

	if(contents)
		parm[5]->Int = tr.Contents;

	if(surfparm)
		parm[6]->Int = tr.SurfParms.GetParms();

	if(tr.MaterialPtr != NULL)
		g_pvTraceShader->Pointer = (void*)tr.MaterialPtr->GetName().cstr();
	else
		g_pvTraceShader->Pointer = (char*)g_pScript->NullString;

	g_pvTraceSurfparm->Int = tr.SurfParms.GetParms();
	g_pvTraceContent->Int = tr.Contents;

	g_pvTraceBone->Int = tr.Bone;

	if(g_pvTraceBone->Int < -1)
		g_pvTraceBone->Int = -1;

	result->Float = tr.Fraction;
}

//-----------------------------------------------------------------------
// proto int	SetCurrentMap(int map)
//-----------------------------------------------------------------------
BaseWorld* NMETHOD SetCurrentMap(BaseWorld *map)
{
	BaseWorld *prev = BaseWorld::Current();

	if(map != NULL)
		map->SetCurrent();

	return prev;
}

//-----------------------------------------------------------------------
// proto void UpdateCubeMap(class ent, int size, string name)
//-----------------------------------------------------------------------
void NMETHOD UpdateCubeMap(Entity* ent, int size, const char* name)
{
#ifndef ENF_CONSOLE
	if(BaseWorld::Current())
		BaseWorld::Current()->CaptureEnvMap(GetEngine()->GetPipeline(), ent->IGetTranslation(), size, name);
#endif
}

//-----------------------------------------------------------------------
float NMETHOD GetWorldTime()
{
	if(BaseWorld::Current())
		return (float)BaseWorld::Current()->GetTime();

	return 0.0f;
}

//-----------------------------------------------------------------------
SCAPI(FileExist)
{
	result->Int = GetFileSystem()->FileExists(parm[0]->String) ? 1 : 0;
}

//-----------------------------------------------------------------------
SCAPI(OpenFile)
{
#ifdef ENF_CONSOLE
	if((parm[1]->Int & FILEMODE_MASK) == FILEMODE_WRITE)
	{
		result->Pointer = NULL;
		return;
	}
#endif
	result->Pointer = (void *)GetFileSystem()->Open(parm[0]->String, (FileMode)parm[1]->Int);
}

//-----------------------------------------------------------------------
SCAPI(ReadFile)
{
	if(parm[0]->Pointer)
	{
		VFile *vf = (VFile *)parm[0]->Pointer;

		result->Int = vf->Read(parm[1]->Pointer, parm[2]->Int);
	}
	else
		result->Int = 0;
}

//-----------------------------------------------------------------------
SCAPI(CloseFile)
{
	VFile *vf;

	vf = (VFile *)parm[0]->Pointer;
	if(vf)
		vf->Close();
}

//-----------------------------------------------------------------------
SCAPI(FGets)
{
VFile *vf;
int l = -1;

	if((vf = (VFile *)parm[0]->Pointer) != NULL)
	{
		char tmp[65536];

		if(vf->Gets(tmp,sizeof(tmp)))
		{
			l = strlen(tmp);
			if(tmp[l - 2] == 10 || tmp[l - 2] == 13)
			{
				tmp[l - 2] = 0;
				l -= 2;
			}
			else
			{
				if(tmp[l - 1] == 10 || tmp[l - 1] == 13)
				{
					tmp[l - 1] = 0;
					l -= 1;
				}
			}

			g_pScript->CopyString(tmp, &parm[1]->String, true);
		}
	}
	result->Int = l;
}

//-----------------------------------------------------------------------
int NMETHOD MakeDirectory(CStr name)
{
	return GetFileSystem()->MakeDirectory(name) ? 1 : 0;
}

//-----------------------------------------------------------------------
int NMETHOD DeleteFile(CStr name)
{
	if( name.ncmp("$profile:", 9) != 0 && name.ncmp("$saves:", 7) != 0 )
		return 0;

	return GetFileSystem()->Delete(name) ? 1 : 0;
}

//-----------------------------------------------------------------------
int NMETHOD CopyFile(CStr source, CStr dest)
{
	if( dest.ncmp("$profile:", 9) != 0 && dest.ncmp("$saves:", 7) != 0 )
		return 0;

	FileSystem* fs = GetFileSystem();
	int result = 0;

	VFile* srcFile = fs->Open(source, FILEMODE_READ);
	VFile* targFile = fs->Open(dest, FILEMODE_WRITE);

	if(srcFile && targFile)
		result = fs->CopyFile(targFile, srcFile) ? 1 : 0;

	if(srcFile)
		srcFile->Close();

	if(targFile)
		targFile->Close();

	return result;
}

//-----------------------------------------------------------------------
SCAPI(FindFile)
{
	SFileDescription desc;
	VFindFile* ff = GetFileSystem()->FindFile(parm[0]->String, desc, parm[3]->Int);
	if(ff)
	{
		GetScriptContext()->CopyString(desc.FileName, &parm[1]->String, true);
		parm[2]->Int = desc.Attributes;
	}
	result->Pointer = ff;
}

//-----------------------------------------------------------------------
SCAPI(FindNextFile)
{
	SFileDescription desc;
	VFindFile* ff = reinterpret_cast<VFindFile*>(parm[0]->Pointer);
	if(ff)
	{
		if(ff->GetNext(desc))
		{
			GetScriptContext()->CopyString(desc.FileName, &parm[1]->String, true);
			parm[2]->Int = desc.Attributes;
			result->Int = true;
			return;
		}
	}

	result->Int = false;
}
//-----------------------------------------------------------------------
void NMETHOD CloseFindFile(VFindFile* ff)
{
	if(ff)
		ff->Close();
}

//-----------------------------------------------------------------------
SCAPI(FPrint)
{
IVariable *var;
const Variable *dv;

float *f;
char tmp[65536];
VFile *vf;
int pos = 0;

	if((vf = (VFile *)parm[0]->Pointer) != NULL)
	{
		var = parm[1];
		dv = g_pScript->GetParmDef(1);

		switch(dv->GetVType())
		{
		case (VARTYPE_STRING):
			strcpy(&tmp[pos], var->String);
			break;

		case (VARTYPE_INT):
			sprintf(&tmp[pos], "%d", var->Int);
			break;

		case (VARTYPE_FLOAT):
			sprintf(&tmp[pos], "%g", (double)var->Float);
			break;

		case (VARTYPE_VECTOR):
			f=(float *)(var->Pointer);
			sprintf(&tmp[pos],"%g %g %g",(double)f[0],(double)f[1],(double)f[2]);
			break;

		default:
			sprintf(&tmp[pos],"%x",var->Int);
			break;
		}

		vf->Write(tmp, strlen(tmp));
	}
}

//-----------------------------------------------------------------------
SCAPI(FPrintln)
{
VFile *vf;

	if((vf = (VFile *)parm[0]->Pointer) != NULL)
	{
		FPrint(parm, result);
		vf->Write("\n", 1);
	}
}

//-----------------------------------------------------------------------
SCAPI(Print)
{
	//if(MainConfig::m_bEnableConsole)
	{
		Variable lvar;
		char tmp[65536];

		lvar = *g_pScript->GetParmDef(0);
		lvar.Pointer = parm[0]->Pointer;

		Variable2Text(&lvar, tmp, false, true);

		LogF(LL_NORMAL, tmp);
	}
}

//-----------------------------------------------------------------------
void NMETHOD Error2(const char* title, const char* text)
{
	SString<> str;

	str = title;
	str = str + "\n";
	str = str + text;
	Error(str.cstr());
}

//-----------------------------------------------------------------------
SCAPI(DPrint)
{
	enf::LogF(LL_DEBUG, parm[0]->String);

#ifdef WIN32
	char buff[256];
	::GetModuleFileName(NULL, buff, sizeof(buff));
	if(strstr(buff, "Workbench"))
		return;

	DumpStack(NULL, NULL);
#endif
}

//-----------------------------------------------------------------------
SCAPI(AddLight)
{
LightType ltype = (LightType)parm[1]->Int;
uint flags = (uint)parm[2]->Int;
LightHandle lightHandle;
Entity* ent = Parm2Entity(parm[0]);

	FColor color;
	color.r = ((const float *)parm[4]->Pointer)[0];
	color.g = ((const float *)parm[4]->Pointer)[1];
	color.b = ((const float *)parm[4]->Pointer)[2];
	color.a = 1.0f;

	if(flags & LFL_DYNAMIC)
	{

		lightHandle = ent->GetWorld()->CreateDLight(
						ltype,
						ent,
						flags,
						parm[3]->Float,
						color);
	}
	else
	{
		lightHandle = ent->GetWorld()->CreateLight(
						ltype,
						ent->IGetTranslation(),
						Vector3().Zero(),
						flags,
						parm[2]->Float,
						color);
		
		if(ltype == LT_SPOT)
			ent->GetWorld()->SetLightDirection(lightHandle, ent->IGetMatrix().GetRow(0));
	}
	result->Pointer = lightHandle;
}

//-----------------------------------------------------------------------
uint NMETHOD SetLightFlags(LightHandle handle, uint flags)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		return wrld->SetLightFlags(handle, flags);
	}
	return 0;
}

//-----------------------------------------------------------------------
uint NMETHOD ClearLightFlags(LightHandle handle, uint flags)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		return wrld->ClearLightFlags(handle, flags);
	}
	return 0;
}

//-----------------------------------------------------------------------
uint NMETHOD RemoveLight(LightHandle handle)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		wrld->DestroyLight(handle);
	}
	return true;
}

//-----------------------------------------------------------------------
uint NMETHOD SetLightEx(LightHandle handle, float radius, const Vector3& color)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		wrld->SetLightDiffuseColor(handle, FColor(color.x * FCOLOR_SCALE, color.y * FCOLOR_SCALE, color.z * FCOLOR_SCALE, 1.0f));
		wrld->SetLightSpecularColor(handle, FColor(color.x * FCOLOR_SCALE, color.y * FCOLOR_SCALE, color.z * FCOLOR_SCALE, 1.0f));
		wrld->SetLightRange(handle, radius);
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------
int NMETHOD SetLightTexture(LightHandle handle, const char* texturename)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		wrld->SetLightLUPTexture(handle, texturename);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------
int NMETHOD SetLightCone(LightHandle handle, float fov)
{
	BaseWorld* wrld = BaseWorld::Current();
	if(wrld && wrld->IsLightValid(handle))
	{
		wrld->SetLightCone(handle, fov * 0.5f);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------
Vector3 NMETHOD GetOrigin(Entity* ent)
{
	AssertEntity(ent);

	return ent->GetOrigin();
}

//----------------------------------------------------------------------
//proto vector GetMatrix(class ent, int row)
//-----------------------------------------------------------------------
Vector3 NMETHOD GetMatrix(Entity* ent, uint axis)
{
	AssertEntity(ent);

	if(axis > 3)
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);

	return ent->IGetMatrix().GetRow(axis);
}

//----------------------------------------------------------------------
//proto void GetMatrix4(class ent, vector mat[])
// precte aktualni matici entity. Precte jen tolik vektoru, jak velke je pole.
// Je tedy mozne precist jen 3x3 rotacni matici.
//-----------------------------------------------------------------------
SCAPI(GetMatrix4)
{
	const Variable *v = g_pScript->GetParmDef(1);

	if(v->GetVType() != VARTYPE_VECTOR ||
		v->Spec & (VSPEC_CONST|VSPEC_PROTECT))
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);

	CopyMem(parm[1]->Pointer, &Parm2Entity(parm[0])->IGetMatrix(), sizeof(Vector3) * ((v->Dim[1] < 4) ? v->Dim[1] : 4));
}

//-----------------------------------------------------------------------
SCAPI(RollPitchYawMatrix)
{
	const Vector3& ang = *(Vector3*)parm[0]->Pointer;

	((Matrix43*)parm[1]->Pointer)->RollPitchYaw(ang[ROLL], ang[PITCH], ang[YAW]);
}

//-----------------------------------------------------------------------
SCAPI(AroundVectorMatrix)
{
	((Matrix43*)parm[2]->Pointer)->AroundVector((float *)parm[0]->Pointer, parm[1]->Float);
}

//-----------------------------------------------------------------------
SCAPI(VectorMatrixMultiply4)
{
	*((Vector3 *)result->Pointer) = ((Matrix43*)parm[0]->Pointer)->TransformCoord(*(Vector3 *)parm[1]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(VectorMatrixMultiply3)
{
	*((Vector3 *)result->Pointer) = ((Matrix43*)parm[0]->Pointer)->TransformVector(*(Vector3 *)parm[1]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(VectorMatrixInvMultiply4)
{
	*((Vector3 *)result->Pointer) = ((Matrix43*)parm[0]->Pointer)->InvTransformCoord(*(Vector3 *)parm[1]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(VectorMatrixInvMultiply3)
{
	*((Vector3 *)result->Pointer) = ((Matrix43*)parm[0]->Pointer)->InvTransformVector(*(Vector3 *)parm[1]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(MatrixMultiply3)
{
	*(Matrix33*)parm[2]->Pointer = ((Matrix43*)parm[0]->Pointer)->GetOrientation() * ((Matrix43*)parm[1]->Pointer)->GetOrientation();
}

//-----------------------------------------------------------------------
SCAPI(MatrixMultiply4)
{
	*(Matrix43*)parm[2]->Pointer = *((Matrix43*)parm[0]->Pointer) * *((Matrix43*)parm[1]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(MatrixInvMultiply3)
{
	*(Matrix33*)parm[2]->Pointer = ((Matrix43*)parm[0]->Pointer)->GetOrientation().InvTransform(((Matrix43*)parm[1]->Pointer)->GetOrientation());
}

//-----------------------------------------------------------------------
SCAPI(MatrixInvMultiply4)
{
	*(Matrix43*)parm[2]->Pointer = ((Matrix43*)parm[0]->Pointer)->InvTransform(*((Matrix43*)parm[1]->Pointer));
}

//----------------------------------------------------------------------
//proto void MatrixQuat(vector mat[3], float q[4])
//----------------------------------------------------------------------
SCAPI(MatrixQuat)
{
	const Matrix43& mat = *(Matrix43*)parm[0]->Pointer;
	Quaternion quat(mat);
	*(Quaternion*)parm[1]->Pointer = quat;
}

//----------------------------------------------------------------------
//proto void QuatLerp(float out[4], float q1[4], float q2[4], float frac)
//----------------------------------------------------------------------
SCAPI(QuatLerp)
{
	*(Quaternion*)parm[0]->Pointer = ((const Quaternion*)parm[1]->Pointer)->Lerp(*(const Quaternion*)parm[2]->Pointer, parm[3]->Float);
}

//----------------------------------------------------------------------
//proto void QuatMatrix(float q[4], vector mat[3])
//----------------------------------------------------------------------
SCAPI(QuatMatrix)
{
	Matrix43 mat = *(Quaternion*)parm[0]->Pointer;
	float* pf = (float*)parm[1]->Pointer;

	pf[0] = mat._11;
	pf[1] = mat._12;
	pf[2] = mat._13;

	pf[3] = mat._21;
	pf[4] = mat._22;
	pf[5] = mat._23;

	pf[6] = mat._31;
	pf[7] = mat._32;
	pf[8] = mat._33;

}

//----------------------------------------------------------------------
//proto void QuatMultiply(float out[4], float q1[4], float q2[4])
//----------------------------------------------------------------------
SCAPI(QuatMultiply)
{
	*(Quaternion*)parm[0]->Pointer = ((Quaternion*)parm[1]->Pointer)->Multiply(*(Quaternion*)parm[2]->Pointer);
}

//-----------------------------------------------------------------------
SCAPI(GetBoundBox)
{
Entity *th = Parm2Entity(parm[0]);

	if(th->GetInstance())
	{
		AAB bbox = th->GetVObject()->GetBoundingBox();
		*(Vector3 *)parm[1]->Pointer = bbox.GetMins();
		*(Vector3 *)parm[2]->Pointer = bbox.GetMaxs();
	}
	else
	{
		(*(Vector3 *)parm[1]->Pointer).Zero();
		(*(Vector3 *)parm[2]->Pointer).Zero();
	}
}

//-----------------------------------------------------------------------
SCAPI(GetCBoundBox)
{
Entity *th = Parm2Entity(parm[0]);

	*(Vector3 *)parm[1]->Pointer = th->GetBBox().GetMins();
	*(Vector3 *)parm[2]->Pointer = th->GetBBox().GetMaxs();
}

//-----------------------------------------------------------------------
SCAPI(CreateModel)
{
Vector3 mins, maxs;
Entity *ent;

	ent = Parm2Entity(parm[0]);
	mins = (float *)parm[1]->Pointer;
	maxs = (float *)parm[2]->Pointer;

	if(mins.x > maxs.x ||
		mins.y > maxs.y ||
		mins.z > maxs.z)
		return;
		//g_pScript->Error("Can't create model with mins > maxs");

	MModel* model = ENF_NEW MModel();
	
	model->CreateFromBox(mins, maxs);

	ent->SetObject(model);

	ent->DirtyFlags();
}

//-----------------------------------------------------------------------
SCAPI(RemoveModel)
{
	Entity* ent = Parm2Entity(parm[0]);
	if(ent)
	{
		MModel* mod = MModel::SafeCast(ent->GetVObject());
	
		if(mod)
		{
			ent->SetObject(NULL);

			mod->Free();
			mod->Release(true);
			ent->DirtyFlags();
		}
	}
}

//-----------------------------------------------------------------------
uint NMETHOD UpdateEntity(Entity *ent)
{
	AssertEntity(ent);

	if(ent->GetPhysics())
	{
		ent->GetPhysics()->Synchronize();
	}
	return ent->Update();
}

//-----------------------------------------------------------------------
void NMETHOD SetOrigin(Entity *th, const Vector3& org)
{
	AssertEntity(th);
	if(BadVec(org))
	{
		g_pScript->Error("SetOrigin() has NAN origin");
		return;
	}

	th->SetOrigin(org);
}

//-----------------------------------------------------------------------
void NMETHOD SetMatrix(Entity* th, const Matrix43& mat)
{
	AssertEntity(th);

	if(BadVec(mat[0].v) ||
		BadVec(mat[1].v) ||
		BadVec(mat[2].v) ||
		BadVec(mat[3].v))
		g_pScript->Error("SetMatrix() contains NAN");

	th->SetMatrix(mat);
}

//-----------------------------------------------------------------------
void NMETHOD SetWeight(Entity *ent, float weight)
{
	AssertEntity(ent);
	
	if(ent->GetPhysics())
		ent->GetPhysics()->SetMass(weight);
}

//-----------------------------------------------------------------------
uint NMETHOD SetRenderViewMask(Entity* ent, uint mask)
{
	AssertEntity(ent);
	if(ent->GetInstance())
	{
		return ent->GetInstance()->SetCameraMask(mask);
	}

	return 0;
}

//-----------------------------------------------------------------------
void NMETHOD SetGUIWidget(Entity *th, uint index, Widget* w)
{
	AssertEntity(th);
	th->SetRTTexture(RTTextureWidget::SafeCast(w));
}

//-----------------------------------------------------------------------
void NMETHOD SetRenderView(Entity *th, uint camindex, uint width, uint height)
{
	AssertEntity(th);
	if(th->GetWorld() == NULL)
		return;
	th->GetInstance()->SetRenderView(th, camindex, width, height);
}

//-----------------------------------------------------------------------
SCAPI(GetRenderView)
{
	Entity* ent = Parm2Entity(parm[0]);
	AssertEntity(ent);

	VObjectInstance* inst = ent->GetInstance();
	if(inst)
	{
		parm[1]->Int = (int)inst->GetRenderView();

		RenderTarget* rt = ent->GetWorld()->GetCameraRenderTarget(inst->GetRenderView());
		if(rt)
		{
			parm[2]->Int = rt->GetWidth();
			parm[3]->Int = rt->GetHeight();
		}
		else
		{
			parm[2]->Int = 0;
			parm[3]->Int = 0;
		}
	}
	else
	{
		parm[1]->Int = 0;
		parm[2]->Int = 0;
		parm[3]->Int = 0;
	}
}

//-----------------------------------------------------------------------
uint NMETHOD SetFlags(Instance* inst, uint flag)
{
	Entity* th = AssertEntity(inst);

	//filter out system flags
	flag &= ~TFL_PRIVATE_FLAGS;

	uint prevflags = th->GetFlags() & ~TFL_PRIVATE_FLAGS;

	th->SetFlags(flag);

	return prevflags;
}

//-----------------------------------------------------------------------
uint NMETHOD ClearFlags(Entity *th, uint flag)
{
	AssertEntity(th);

	//filter out system flags
	flag = flag & ~TFL_PRIVATE_FLAGS;

	return th->ClearFlags(flag);
}

//-----------------------------------------------------------------------
uint NMETHOD GetFlags(Entity *th)
{
	return AssertEntity(th)->GetFlags();
}

//-----------------------------------------------------------------------
Entity* NMETHOD GetNextEntity(Entity *ent, Class *cl)
{
	return BaseWorld::Current()->GetNextEntity(ent, cl);
}

//-----------------------------------------------------------------------
//proto native int		GetNumActiveEntities(_entity worldEntity);
//-----------------------------------------------------------------------
uint NMETHOD GetNumActiveEntities(Entity* worldEntity)
{
	if(!worldEntity || !worldEntity->GetWorld())
		return 0;

	return worldEntity->GetWorld()->GetNumActiveEntities();
}

//-----------------------------------------------------------------------
//proto native _entity GetActiveEntity(_entity worldEntity, int index);
//-----------------------------------------------------------------------
Entity* NMETHOD GetActiveEntity(Entity* worldEntity, uint index)
{
	if(!worldEntity || !worldEntity->GetWorld())
		return NULL;

	if(worldEntity->GetWorld()->GetNumActiveEntities() <= index)
		return NULL;

	return worldEntity->GetWorld()->GetActiveEntity(index);
}

//-----------------------------------------------------------------------
void NMETHOD SetFrame(Entity *ent, uint slot, float frame)
{
	AssertEntity(ent);

	MeshObjectAnimatedInstance* inst = ((MeshObjectAnimatedInstance*)ent->GetInstance());

	if(inst == NULL || inst->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return;


	if(ent->GetInstance() == NULL)
	if(slot >= MAX_ANIM_SLOTS)
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);

	inst->SetFrame(ent, slot, frame);
}

//-----------------------------------------------------------------------
//proto void		SetAnimMask(class ent, int mask)
//-----------------------------------------------------------------------
void NMETHOD SetAnimMask(Entity *ent, uint mask)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return;

	((MeshObjectAnimatedInstance*)ent->GetInstance())->SetAnimMask(mask);
}

//-----------------------------------------------------------------------
//proto int		ClearAnimMask(class ent, int mask)
//-----------------------------------------------------------------------
uint NMETHOD ClearAnimMask(Entity *ent, uint mask)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return 0;

	return ((MeshObjectAnimatedInstance *)ent->GetInstance())->ClearAnimMask(mask);
}

//-----------------------------------------------------------------------
//proto void ChangeAnimSlotMask(class ent, int slot, float blendin, BoneMask mask)
//-----------------------------------------------------------------------
void NMETHOD ChangeAnimSlotMask(Entity *ent, uint slot, float blendin, const CBoneMask* bmask)
{
	BoneMask mask;

	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return;

	if(slot >= MAX_ANIM_SLOTS)
	{
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);
		return;
	}

	if(bmask == NULL)
		mask.SetAll();
	else
		mask = BoneMask(bmask->Mask[0], bmask->Mask[1], bmask->Mask[2]);

	MeshObjectAnimatedInstance *ai = (MeshObjectAnimatedInstance *)ent->GetInstance();

	ai->SetAnimSlotBoneMask(slot, mask, blendin);
}

//-----------------------------------------------------------------------
//proto void ChangeAnimSlotFPS(class ent, int slot, int fps)
//-----------------------------------------------------------------------
void NMETHOD ChangeAnimSlotFPS(Entity *ent, uint slot, int fps)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return;

	if(slot >= MAX_ANIM_SLOTS)
	{
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);
		return;
	}

	MeshObjectAnimatedInstance *ai = (MeshObjectAnimatedInstance *)ent->GetInstance();
	ai->SetAnimSlotFPS(slot, fps);
}

//-----------------------------------------------------------------------
//proto bool SetMorphState(class ent, string morph, float value)
//-----------------------------------------------------------------------
int NMETHOD SetMorphState(Entity *ent, const char* name, float value)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return false;

	MeshObjectAnimatedInstance *ai = (MeshObjectAnimatedInstance *)ent->GetInstance();
	return (int)ai->SetMorphState(name, value);
}

//-----------------------------------------------------------------------
//proto native int IsAnimSlotPlaying(class ent, int mask)
//-----------------------------------------------------------------------
uint NMETHOD IsAnimSlotPlaying(Entity *ent, uint mask)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL || ent->GetInstance()->GetTypeID() != MeshObjectAnimatedInstanceTypeID)
		return 0;

	MeshObjectAnimatedInstance *ai = (MeshObjectAnimatedInstance *)ent->GetInstance();

	return ai->IsAnimSlotPlaying(mask);
}

//-----------------------------------------------------------------------
//proto void SetAnimSlot(class ent, int slot, vobject anim, float blendtime, float blendfac, CBoneMask mask, int fps, int flags)
//-----------------------------------------------------------------------
void NMETHOD SetAnimSlot(Entity *ent, uint slot, Animation* anim, float blendin, float blendout, const CBoneMask* bmask, int fps, int flags)
{
	AssertEntity(ent);
	MeshObjectAnimatedInstance* ai = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());

	if(ai == NULL)
		return;

	if(slot >= MAX_ANIM_SLOTS)
	{
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);
		return;
	}


	BoneMask mask;

	if(bmask != NULL)
		mask = BoneMask(bmask->Mask[0], bmask->Mask[1], bmask->Mask[2]);

	ai->SetAnimSlot(ent, slot, anim, NULL, blendin, blendout, (bmask == NULL) ? NULL : &mask, fps, flags);
}

//-----------------------------------------------------------------------
void NMETHOD SetBone(Entity* ent, uint bone, enf::Vector3& angles, enf::Vector3& trans, float scale)
{
	AssertEntity(ent);
	MeshObjectAnimatedInstance* ai = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());

	if(ai == NULL)
		return;

	ai->SetAdditionalLocalTransform(ent, bone, angles, trans, scale);
}

//-----------------------------------------------------------------------
int NMETHOD SetBoneMatrix(Entity* ent, uint bone, Matrix43& mat)
{
	AssertEntity(ent);
	MeshObjectAnimatedInstance* ai = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());

	if(ai == NULL)
		return false;
	ai->SetAdditionalLocalTransform(ent, bone, mat);
	return true;
}

//-----------------------------------------------------------------------
void NMETHOD SetBoneGlobal(Entity* ent, uint bone, Matrix43& mat)
{
	AssertEntity(ent);
	MeshObjectAnimatedInstance* ai = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());

	if(ai == NULL)
		return;

	ai->SetAdditionalGlobalTransform(ent, bone, mat);
}

//-----------------------------------------------------------------------
//	proto bool GetBoneMatrix(class ent, int bone, vector mat[4]);
//-----------------------------------------------------------------------
uint NMETHOD GetBoneMatrix(Entity* ent, uint bone, Matrix43& mat)
{
	AssertEntity(ent);

	MeshObjectAnimatedInstance* mi = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());
	if(mi == NULL)
		return false;

	MeshObject* sm = MeshObject::Cast(mi->GetVObject());
	uint index = sm->InternNodeIndex(bone);

	if(index == INDEX_NOT_FOUND)
		return false;

	mat = mi->GetNodeTransform(index);
	return true;
}

//-----------------------------------------------------------------------
//	proto bool GetBoneLocalMatrix(class ent, int bone, vector mat[4]);
//-----------------------------------------------------------------------
uint NMETHOD GetBoneLocalMatrix(Entity* ent, uint bone, Matrix43& mat)
{
	AssertEntity(ent);

	MeshObjectAnimatedInstance* mi = MeshObjectAnimatedInstance::SafeCast(ent->GetInstance());
	if(mi == NULL)
		return false;

	mat = mi->GetLocalOrientation(bone).ToMatrix();
	return true;
}

//-----------------------------------------------------------------------
int NMETHOD BumpFrame(Entity* ent, float timeslice, int loop)
{
	AssertEntity(ent);

	if(ent->GetInstance() == NULL)
		return 0;

	timeslice = enf_max(0.0f, timeslice);
	timeslice = enf_min(timeslice, 1.0f);
	int frame = ent->GetInstance()->UpdateAnim(ent, timeslice, loop);

	return frame;
}

//-----------------------------------------------------------------------
int NMETHOD BumpFrameEx(Entity* th, float timeslice, int loop, Vector3& lin, Vector3& ang)
{
	AssertEntity(th);

	if(th->GetInstance() == NULL)
		return 0;

	AnimMovement movement;

	timeslice = enf_max(0.0f, timeslice);
	timeslice = enf_min(timeslice, 1.0f);
	int frame = th->GetInstance()->UpdateAnim(th, timeslice, loop, &movement);

	lin = movement.Linear;
	ang = movement.Angular;

	return frame;
}

//-----------------------------------------------------------------------
//	proto native void	ResetParticlePosition(class ent)
//-----------------------------------------------------------------------
void NMETHOD ResetParticlePosition(Entity* ent)
{
	AssertEntity(ent);

	ParticleEffectInstance* pi = ParticleEffectInstance::SafeCast(ent->GetInstance());
	if(pi == NULL)
		return;
	pi->ResetPosition(ent);
}

//-----------------------------------------------------------------------
//	proto native int GetParticleCount(class ent)
//-----------------------------------------------------------------------
uint NMETHOD GetParticleCount(Entity* ent)
{
	AssertEntity(ent);

	ParticleEffectInstance* pi = ParticleEffectInstance::SafeCast(ent->GetInstance());
	if(pi == NULL)
		return 0;

	return pi->GetTotalActiveParticles();
}
//-----------------------------------------------------------------------
//	proto void SetParticleParm(class ent, int emitor, int parameter, void value)
//-----------------------------------------------------------------------
SCAPI(SetParticleParm)
{
	Entity *th = Parm2Entity(parm[0]);

	ParticleEffectInstance* pi = ParticleEffectInstance::SafeCast(th->GetInstance());
	if(pi == NULL)
		return;

	pi->SetParm(parm[1]->Int, (EmitorParm)parm[2]->Int, parm[3]);
}

//-----------------------------------------------------------------------
//	proto void GetParticleParm(class ent, int emitor, int parameter, void value)
//-----------------------------------------------------------------------
SCAPI(GetParticleParm)
{
	Entity *th = Parm2Entity(parm[0]);

	ParticleEffectInstance* pi = ParticleEffectInstance::SafeCast(th->GetInstance());
	if(pi == NULL)
		return;

	pi->GetParm(parm[1]->Int, (EmitorParm)parm[2]->Int, parm[3]);
}

//-----------------------------------------------------------------------
//	proto void GetParticleParmOriginal(class ent, int emitor, int parameter, void value)
//-----------------------------------------------------------------------
SCAPI(GetParticleParmOriginal)
{
	Entity *th = Parm2Entity(parm[0]);

	ParticleEffect* pe = ParticleEffect::SafeCast(th->GetVObject());
	if(pe == NULL)
		return;

	pe->GetParm(parm[1]->Int, (EmitorParm)parm[2]->Int, parm[3]);
}

//-----------------------------------------------------------------------
//	proto int		GetParticleEmitors(class ent, string emitors[], int max)
//-----------------------------------------------------------------------
SCAPI(GetParticleEmitors)
{
Entity *th = Parm2Entity(parm[0]);

	result->Int = 0;

	ParticleEffectInstance* pi = ParticleEffectInstance::SafeCast(th->GetInstance());
	if(pi == NULL)
		return;

	ParticleEffect* pe = pi->GetVObject();

	const char* names[64];

	char **ptr = (char**)parm[1]->Pointer;
	const Variable* varDef = ScriptContext::m_pCurrent->GetParmDef(1);

	if(ptr)
	{
		result->Int = pe->GetEmitorNames(names, 64);

		for(int n = 0; n < result->Int; n++)
			g_pScript->CopyString(names[n], &ptr[n], true);
	}
	else
		result->Int = pe->GetEmitorNames(NULL, 0);
}


//-----------------------------------------------------------------------
VObjectResource* NMETHOD GetObject(const char* name)
{
	enf_assert(name);

	if(name[0] == 0)
		return NULL;

	return GetResources()->GetVObject(name);
}

//-----------------------------------------------------------------------
void NMETHOD ReleaseObject(VObjectResource* obj, uint force)
{
	if(obj != NULL)
	{
		//these are created by world and are destroyed there too.
		if(obj->HasType(MModelTypeID))
		{
			return;
		}

		//if(obj->GetName() != NULL)
		{
			if(obj->RefCount() == 0)
				g_pScript->Error("Object is already released");

			obj->Release(force != 0);
		}
	}
}

//-----------------------------------------------------------------------
void NMETHOD SelectObject(Entity* th, VObject* obj)
{
	AssertEntity(th);

	th->SetObject(obj);
}

//-----------------------------------------------------------------------
uint NMETHOD SetEventMask(Instance* inst, uint mask)
{
	return AssertEntity(inst)->SetEventMask(mask);
}

//-----------------------------------------------------------------------
uint NMETHOD ClearEventMask(Entity *th, uint mask)
{
	AssertEntity(th);
	return th->ClearEventMask(mask);
}

//-----------------------------------------------------------------------
uint NMETHOD GetEventMask(Entity *th)
{
	return AssertEntity(th)->GetEventMask();
}

//-----------------------------------------------------------------------
uint NMETHOD KeyState(KeyCode kc)
{
	InputDevice* idev = GetInputDevice();

	if(!idev)
		return 0;

	return GetInputDevice()->KeyState(kc);
}

//-----------------------------------------------------------------------
int NMETHOD MouseState(MouseKey mk)
{
	InputDevice* idev = GetInputDevice();

	if(!idev)
		return 0;

	int val = 0;
	switch(mk)
	{
	case MS_LB:
		return idev->GetMouseButton(0);
	case MS_MB:
		return idev->GetMouseButton(1);
	case MS_RB:
		return idev->GetMouseButton(2);
	case MS_X:
	case MS_Y:
		{
			float mx, my, mw;
			idev->GetMouseState(&mx, &my, &mw);

			if(mk == MS_X)
				return (int)mx;
			else
				return (int)my;
		}
	};
	return 0;
}

//	proto void GetMousePos(out int x, out int y)
//-----------------------------------------------------------------------
SCAPI(GetMousePos)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);
	enf::WidgetManager* wm = enf::GetWidgetManager();
	wm->GetLastMousePos(&parm[0]->Int, &parm[1]->Int);
}
//-----------------------------------------------------------------------
SCAPI(GetScreenSize)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);
	enf::WidgetManager* wm = enf::GetWidgetManager();
	parm[0]->Int = wm->GetWidth();
	parm[1]->Int = wm->GetHeight();
}

//-----------------------------------------------------------------------
Widget* NMETHOD GetWidgetUnderCursor()
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);
	enf::WidgetManager* wm = enf::GetWidgetManager();
	return wm->GetWidgetUnderCursor();
}

//-----------------------------------------------------------------------
Vector3 NMETHOD GetAngles(Entity* ent)
{
	AssertEntity(ent);

	return ent->GetAngles();
}

//-----------------------------------------------------------------------
Vector3 NMETHOD GetLocalAngles(Entity* ent)
{
	AssertEntity(ent);

	return ent->GetLocalAngles();
}

//-----------------------------------------------------------------------
void NMETHOD SetAngles(Entity *th, const Vector3& vec)
{
	AssertEntity(th);
	if(BadVec(vec))
	{
		g_pScript->Error("SetAngles() has NAN angles");
		return;
	}

	th->SetAngles(vec);
}

//-----------------------------------------------------------------------
void NMETHOD SetFixedLOD(Entity *th, int lod)
{
	AssertEntity(th);
	if(th->GetInstance())
	{
		th->GetInstance()->SetForceLOD(lod < 0 ? NO_FORCE_LOD : lod);
	}
}

//-----------------------------------------------------------------------
uint NMETHOD AddChild(Entity* pent, Entity* cent, uint pivot)
{
	pent = AssertEntity(pent);
	cent = AssertEntity(cent);

	if(cent == pent)
	{
		g_pScript->Error("AddChild: Child == Parent");
	}

	if(pent->AddChild(cent, pivot))
	{
		if(cent->GetPhysics())
		{
			//TODO: Hack. Is it correct?
			cent->GetPhysics()->SetActive(AS_INACTIVE);
			cent->GetPhysics()->SetDynamic(false);
		}

		return true;
	}

	return false;
}

//-----------------------------------------------------------------------
void NMETHOD RemoveChild(Entity *ent, Entity *child)
{
	AssertEntity(ent)->RemoveChild(child);
}

//-----------------------------------------------------------------------
SCAPI(Angles2Vector)
{
float *vec, *ang;

	ang = (float *)parm[0]->Pointer;
	vec = (float *)result->Pointer;

	float pa = ang[PITCH] * ENF_PI2 / 360.0;
	float ya = ang[YAW] * ENF_PI2 / 360.0;
	float cp = cosf(pa);

	vec[0] = cp * cosf(ya);
	vec[1] = cp * sinf(ya);
	vec[2] = sin(pa);
}

//-----------------------------------------------------------------------
// proto vector UnprojectVector(int camera, float x, float y, vector dir)
//-----------------------------------------------------------------------
SCAPI(UnprojectVector)
{
	Vector3& start = *(Vector3*)result->Pointer;
	Vector3& dir = *(Vector3*)parm[3]->Pointer;

	const CameraParams& cam = World::Current()->GetCamera(parm[0]->Int);

	cam.UnprojectVector(parm[1]->Float, parm[2]->Float, start, dir);
}

//-----------------------------------------------------------------------
SCAPI(ProjectVector)
{
	Matrix44	mat, cmat;
	const CameraParams& cam = World::Current()->GetCamera(parm[0]->Int);

	if(parm[0]->InstancePtr)
		mat = Parm2Entity(parm[1])->IGetMatrix();
	else
		mat.Identity();

	cmat = mat * SetupViewMatrix(cam.m_mMatrix) * cam.m_mLastProjection;
	Vector3 proj = cmat.TransformCoord(*(Vector3*)parm[2]->Pointer);

	((float*)result->Pointer)[0] = ((proj.x + 1.0f) * cam.m_fLastVPWidth * 0.5f) + cam.m_fLastVPX;
	((float*)result->Pointer)[1] = ((-proj.y + 1.0f) * cam.m_fLastVPHeight * 0.5f) + cam.m_fLastVPY;
	((float*)result->Pointer)[2] = proj.z;
}

//-----------------------------------------------------------------------
SCAPI(Vector2Yaw)
{
float *vec = (float *)parm[0]->Pointer;
	float ang = RAD2DEG(atan2f(vec[1], vec[0]));

	while(ang >= 360.0f)
		ang -= 360.0f;

	while(ang < 0)
		ang += 360.0f;

	result->Float = ang;
}

//-----------------------------------------------------------------------
Vector3 NMETHOD Yaw2Vector(float ya)
{
	ya = DEG2RAD(ya);

	return Vector3(::cosf(ya), ::sinf(ya), 0.0f);
}

//-----------------------------------------------------------------------
SCAPI(Vector2Angles)
{
	float	forward;
	float	yaw, pitch;

float *ang;
float *vec;

	vec = (float *)parm[0]->Pointer;
	ang = (float *)(result->Pointer);

	if (vec[1] == 0 && vec[0] == 0)
	{
		yaw = 0;
		if (vec[2] < 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = RAD2DEG(atan2f(vec[1], vec[0]));
		if (yaw < 0)
			yaw += 360;

		forward = (float)enf_sqrte (vec[0] * vec[0] + vec[1] * vec[1]);
		pitch = RAD2DEG(atan2f(-vec[2], forward));
		if (pitch < 0)
			pitch += 360;
	}

	ang[0] = pitch;
	ang[1] = yaw;
	ang[2] = 0;
}

//-----------------------------------------------------------------------
SCAPI(MatrixAngles)
{
	float *mat = (float*)parm[0]->Pointer;
	float *forward;
	float *right;
	float *angles = (float*)result->Pointer;
	float up;

	Vector3 cmat[3];
	CopyMem(cmat, mat, sizeof(Vector3) * 3);

	forward = mat;
	right = mat + 3;
	up = mat[8];

	float xyDist = sqrtf( forward[0] * forward[0] + forward[1] * forward[1] );

	// enough here to get angles?
	if ( xyDist > 0.001f )
	{

		angles[1] = RAD2DEG(atan2f( forward[1], forward[0] ));
		angles[0] = RAD2DEG(atan2f( -forward[2], xyDist ));
		angles[2] = RAD2DEG(atan2f( -right[2], up ));
	}
	else
	{
		angles[1] = RAD2DEG(atan2( -right[0], right[1] ));
		angles[0] = RAD2DEG(atan2( -forward[2], xyDist ));
		angles[2] = 0;
	}
}


//-----------------------------------------------------------------------
SCAPI(QuaternionAngles)
{
	float *q = (float*)parm[0]->Pointer;
	float *angles = (float*)result->Pointer;

	double	r11, r21, r31, r32, r33, r12, r13;
	double	q00, q11, q22, q33;
	double	tmp;

	q00 = q[3] * q[3];
	q11 = q[0] * q[0];
	q22 = q[1] * q[1];
	q33 = q[2] * q[2];

	r11 = q00 + q11 - q22 - q33;
	r21 = 2 * (q[0]*q[1] + q[3]*q[2]);
	r31 = 2 * (q[0]*q[2] - q[3]*q[1]);
	r32 = 2 * (q[1]*q[2] + q[3]*q[0]);
	r33 = q00 - q11 - q22 + q33;

	tmp = fabs(r31);
	if(tmp > 0.999999)
	{
		r12 = 2 * (q[0]*q[1] - q[3]*q[2]);
		r13 = 2 * (q[0]*q[2] + q[3]*q[1]);

		angles[0] = RAD2DEG(0.0f); //roll
		angles[1] = RAD2DEG((float) (-(ENF_PI/2) * r31/tmp)); // pitch
		angles[2] = RAD2DEG((float) atan2(-r12, -r31*r13)); // yaw
	}

	angles[0] = RAD2DEG((float) atan2(r32, r33)); // roll
	angles[1] = RAD2DEG((float) asin(-r31));		 // pitch
	angles[2] = RAD2DEG((float) atan2(r21, r11)); // yaw
}


//-----------------------------------------------------------------------
SCAPI(VVector2Angles)
{
	float	roll, pitch;

float *ang;
Vector3 vec;

	vec = (float *)parm[0]->Pointer;
	ang = (float *)(result->Pointer);

	if (vec.z == 0)
	{
		ang[0] = 0;
		ang[1] = 0;
		ang[2] = 0;
	}
	else
	{
		float up = vec.z * vec.z;

		roll = 360 - RAD2DEG(atan2f(vec.y, enf_sqrte(vec.x * vec.x + up)));
		if (roll < 0)
			roll += 360;

		pitch = RAD2DEG(atan2f(vec.x, enf_sqrte(vec.y * vec.y + up)));
		if (pitch < 0)
			pitch += 360;

		ang[0] = pitch;
		ang[1] = 0;
		ang[2] = roll;
	}
}

//-----------------------------------------------------------------------
void NMETHOD ClearKey(KeyCode kc)
{
	if(GetInputDevice())
		GetInputDevice()->KeyClear(kc);
}

//-----------------------------------------------------------------------
float NMETHOD VectorLength(const Vector3* vec)
{
	return vec->Length();
}
//-----------------------------------------------------------------------
float NMETHOD VectorLengthSq(const Vector3* vec)
{
	return vec->SquaredLength();
}

//-----------------------------------------------------------------------
SCAPI(VectorNormalize)
{
	result->Float = ((Vector3*)parm[0]->Pointer)->Normalize();
}

//-----------------------------------------------------------------------
void NMETHOD SetListenerCamera(uint camera)
{
	BaseWorld::Current()->SetListenerCamera(camera);
}

//-----------------------------------------------------------------------
void NMETHOD SetCamera(const Vector3& pos, const Vector3& ang)
{
	BaseWorld::Current()->SetCamera(0, ang, pos);
}

//-----------------------------------------------------------------------
void NMETHOD GetCamera(uint cam, Matrix43& mat)
{
	mat = BaseWorld::Current()->GetCamera(cam).m_mMatrix;
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraEx(uint cam, const Matrix43& mat)
{
	BaseWorld::Current()->SetCamera(cam, mat);
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraVerticalFOV(uint cam, float fovy)
{
	BaseWorld::Current()->SetCameraVerticalFOV(cam, fovy);
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraNearPlane(uint cam, float nearplane)
{
	BaseWorld::Current()->SetCameraNearPlane(cam, nearplane);
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraFarPlane(uint cam, float farplane)
{
	BaseWorld::Current()->SetCameraFarPlane(cam, farplane);
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraPostProcessEffect(uint cam, uint ppEffect, const char* effectName, const char* name)
{
	BaseWorld::Current()->SetCameraPostProcessEffect(cam, ppEffect, effectName, name);
}

//-----------------------------------------------------------------------
void NMETHOD SetCameraType(uint cam, int type)
{
	BaseWorld::Current()->SetCameraType(cam, (ProjType)type);
}

//-----------------------------------------------------------------------
RenderFont* NMETHOD CreateFont(const char* name, int width, int height)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);

	//TODO: release method?
	return GetEngine()->GetWidgetManager()->GetFont(name);
}

//-----------------------------------------------------------------------
void NMETHOD SetFont(Widget* w, RenderFont* font)
{
	AssertWidget(w);
	
	if(w->GetTypeID() == TextWidgetTypeID)
		((TextWidget*)w)->SetFont(font);
	else if(w->GetTypeID() == ConsoleWidgetTypeID)
		((ConsoleWidget*)w)->SetFont(font);
}

//-----------------------------------------------------------------------
void NMETHOD SetCursorWidget(Widget* w)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);
	enf::WidgetManager* wm = enf::GetWidgetManager();
	wm->SetMousePointer(w);
}

//-----------------------------------------------------------------------
void NMETHOD ShowCursorWidget(int show)
{
	enf::WidgetManager* wm = enf::GetWidgetManager();
	// physically show
	enf::GetWidgetManager()->ShowCursor(show != 0);
}

//-----------------------------------------------------------------------
void NMETHOD ReportMouse(int mousex, int mousey, Widget* rootWidget)
{
	WidgetManager* wm = GetWidgetManager();
	InputDevice* id = GetEngine()->GetInputDevice();

	if(wm && id)
	{
		uint mouseButtons[3];
		float deltax, deltay;
		float mouseWheel;

		mouseButtons[0] = id->GetMouseButton(0);
		mouseButtons[1] = id->GetMouseButton(1);
		mouseButtons[2] = id->GetMouseButton(2);

		id->GetMouseState(&deltax, &deltay, &mouseWheel);
		
		// report mouse status
		wm->ReportMouse(mousex, mousey, mouseButtons, mouseWheel, rootWidget);
	}
}

//-----------------------------------------------------------------------
Widget* NMETHOD CreateWidget(WidgetType type, int left, int top, int width, int height,uint flag, uint color, int sort)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);

	return GetEngine()->GetWidgetManager()->CreateWidget(
		type,
		NULL,
		Rect2D((float)left, (float)top, (float)width, (float)height),
		flag,
		color,
		sort);
}

//-----------------------------------------------------------------------
Widget* NMETHOD CreateWidgetEx(Widget* parent, WidgetType type, int left, int top, int width, int height,uint flag, uint color, int sort)
{
	enf_assert(GetEngine()->GetWidgetManager() != NULL);

	return GetEngine()->GetWidgetManager()->CreateWidget(
		type,
		parent,
		Rect2D((float)left, (float)top, (float)width, (float)height),
		flag,
		color,
		sort);
}

//-----------------------------------------------------------------------
Widget* NMETHOD CreateWidgets(const char* layout, Widget* parentWidget)
{
	enf_assert(layout != NULL);

	//load widget layout
	CFileInputStream inStream(layout);
	bool ok = inStream.IsOpen();
	enf_assert(ok);

	if(!ok)
	{
		enf::LogF(enf::LL_ERROR, "Cannot open layout '%s'", layout);
		return NULL;
	}

	WidgetManager* wm = GetWidgetManager();
	enf_assert(wm != NULL);
	WidgetSource* layoutSource = wm->LoadLayout(inStream);
	enf_assert(layoutSource);

	if(!layoutSource)
	{
		enf::LogF(enf::LL_ERROR, "Cannot load layout '%s'", layout);
		return NULL;
	}

	Widget* layoutRootWidget = wm->CreateWidgets(layoutSource);

	if(layoutRootWidget)
	{
		if(parentWidget)
			parentWidget->AddChild(layoutRootWidget);
		else
			wm->AddChild(layoutRootWidget);
	}

	SAFE_DELETE(layoutSource);
	return layoutRootWidget;
}

//-----------------------------------------------------------------------
#ifdef ENF_PS3
	int NMETHOD IsCircleToCrossSwapped()
	{
		return reinterpret_cast<PS3InputDevice*>(GetInputDevice())->IsCircleToCrossSwapped();
	}
#endif

//-----------------------------------------------------------------------
int NMETHOD LoadWidgetImageSet(const char* filename)
{
	WidgetManager* wm = GetWidgetManager();
	enf_assert(wm != NULL);
	return wm->LoadImageSet(filename) ? 1 : 0;
}

//-----------------------------------------------------------------------
void NMETHOD LoadWidgetStyles(const char* filename)
{
	WidgetManager* wm = GetWidgetManager();
	enf_assert(wm != NULL);
	wm->LoadStyles(filename);
}

//-----------------------------------------------------------------------
Widget* NMETHOD FindWidget(Widget* parent, const char* pathname)
{
	return GetEngine()->GetWidgetManager()->FindWidget(parent, pathname);
}

//-----------------------------------------------------------------------
Widget* NMETHOD FindAnyWidget(Widget* parent, const char* widgetName)
{
	AssertWidget(parent);
	
	return parent->FindAnyWidget(widgetName);
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetEventHandler(Widget* parent, ScriptedWidgetEventHandler* handler)
{
	AssertWidget(parent);
	parent->SetHandler(handler);
}



//-----------------------------------------------------------------------
void NMETHOD SetWidgetUV(Widget* w, const float* uv)
{
	ImageWidget* imgw = ImageWidget::SafeCast(w);

	if(imgw)
	{
		imgw->SetUV(uv);
	}
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetPos(Widget* w, int left, int top, int width, int height)
{
	AssertWidget(w);

	w->SetPos(Rect2D((float)left, (float)top, (float)width, (float)height));
}

//-----------------------------------------------------------------------
void NMETHOD DestroyWidget(Widget* w)
{
	if(w)
		w->DestroyWithChildren();
}

//-----------------------------------------------------------------------
uint NMETHOD LoadWidgetImage(Widget* w, uint num, const char* name)
{
	ImageWidget* imageWidget = ImageWidget::SafeCast(w);
	if(imageWidget)
		return imageWidget->LoadImageFile(num, name);

	return 0;
}

//-----------------------------------------------------------------------
uint NMETHOD UnloadWidgetImage(Widget* w, uint num)
{
	ImageWidget* imageWidget = ImageWidget::SafeCast(w);
	if(imageWidget)
		return imageWidget->SetImageTexture(num, NULL);

	return 0;
}

//-----------------------------------------------------------------------
uint NMETHOD ReloadTexture(const char* path)
{
	ResourceManager* rm = GetResources();
	enf_assert(rm);
	Texture* tex = rm->GetTexture(path);

	if(!tex)
		return 0;

	return rm->ReloadTexture(tex) ? 1 : 0;
}

//-----------------------------------------------------------------------
void NMETHOD SetSkyUserPlanets(bool enabled)
{
	SkyDomeRenderer* rend = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());
	if (rend)
		rend->SetUserPlanets(enabled);
}

//-----------------------------------------------------------------------
int NMETHOD SetSkyPlanet(int index, float azimuthDeg, float zenithDeg)
{
	SkyDomeRenderer* rend = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());
	if (rend)
		return rend->SetPlanetPosition(index, DEG2RAD(azimuthDeg), DEG2RAD(zenithDeg));

	return false;
}

//-----------------------------------------------------------------------
void NMETHOD SetSkyPlanetSize(int index, float angleDeg)
{
	SkyDomeRenderer* rend = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());
	if (rend)
		 rend->SetPlanetSize(index, DEG2RAD(angleDeg));
}

//-----------------------------------------------------------------------
int NMETHOD LoadSkyPresets(CStr presetsFile)
{
	return GetWorldManager()->LoadSkyPresets(presetsFile);
}

//-----------------------------------------------------------------------
int NMETHOD InitSky(CStr presetName)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky)
		return sky->SetPreset(presetName, true);

	return 0;
}

//-----------------------------------------------------------------------
int NMETHOD SetSkyPreset(CStr presetName, float stormy, float dayTime)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky)
	{
		const AtmosphereSetting* preset = GetWorldManager()->GetSkyPresets()->FindPreset(presetName);
	
		if (!preset)
			return 0;

		sky->SetPreset(*preset, dayTime, stormy);
		return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------
int NMETHOD LerpSkyPreset(CStr presetName1, CStr presetName2, float dayTime, float stormy1, float stormy2, float lerpVal)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky)
	{
		const AtmosphereSetting *preset1, *preset2;
		if (!(preset1 = GetWorldManager()->GetSkyPresets()->FindPreset(presetName1)))
			return 0;

		if (!(preset2 = GetWorldManager()->GetSkyPresets()->FindPreset(presetName2)))
			return 0;

		sky->SetPreset(*preset1, *preset2, dayTime, stormy1, stormy2, lerpVal);
		return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------
int NMETHOD LerpSkyPreset3(CStr presetName1, CStr presetName2, CStr presetName3, float dayTime, float stormy1, float stormy2, float stormy3, float w1, float w2, float w3)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky)
	{
		const AtmosphereSetting *preset1, *preset2, *preset3;
		if (!(preset1 = GetWorldManager()->GetSkyPresets()->FindPreset(presetName1)))
			return 0;

		if (!(preset2 = GetWorldManager()->GetSkyPresets()->FindPreset(presetName2)))
			return 0;

		if (!(preset3 = GetWorldManager()->GetSkyPresets()->FindPreset(presetName3)))
			return 0;

		sky->SetPreset(*preset1, *preset2, *preset3, w1, w2, w3, dayTime, stormy1, stormy2, stormy3);
		return 1;
	}

	return 0;
}

//-----------------------------------------------------------------------
void NMETHOD SetStarsObserverPosition(float latitudeDeg, float longitudeDeg)
{
#ifdef REAL_STARS
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky && sky->GetStars())
	{
		sky->GetStars()->SetRealStarsObserverPosition(latitudeDeg, longitudeDeg);
	}	
#endif
}

//-----------------------------------------------------------------------
void NMETHOD SetStarsObserverTime(int year, int month, int day, int hour, int minute, float sec, int offsetSec)
{
#ifdef REAL_STARS
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky && sky->GetStars())
	{
		sky->GetStars()->SetRealStarsObserverTime(year, month, day, hour, minute, sec, offsetSec);
	}	
#endif
}

//-----------------------------------------------------------------------
void NMETHOD SetRealStarAutoUpdate(bool update)
{
#ifdef REAL_STARS
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky && sky->GetStars())
	{
		sky->GetStars()->SetRealStarAutoUpdate(update);
	}	
#endif
}

//-----------------------------------------------------------------------
void NMETHOD SetNightLayerRotMatrix(const Matrix33& mat)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky && sky->GetNightLayer())
	{
		Matrix34 finalMat(mat.GetRow(0), mat.GetRow(1), mat.GetRow(2), Vector3(0.0f));
		sky->GetNightLayer()->SetLocalMatrix(finalMat);
	}
}

//-----------------------------------------------------------------------
void NMETHOD SetStarsRotMatrix(const Matrix33& mat)
{
	SkyDomeRenderer* sky = SkyDomeRenderer::SafeCast(BaseWorld::Current()->GetSkyRenderer());

	if (sky && sky->GetStars())
	{
		Matrix34 finalMat(mat.GetRow(0), mat.GetRow(1), mat.GetRow(2), Vector3(0.0f));
		sky->GetStars()->SetLocalMatrix(finalMat);
	}
}



//-----------------------------------------------------------------------
// proto int GetAddons(string addons[])
//-----------------------------------------------------------------------
SCAPI(GetAddons)
{
	int numAddons = 0;
	AddonManager* addMan = AddonManager::SafeCast(GetEngine()->FindModule(AddonManagerTypeID));
	if(addMan)
	{
		const Variable* varDef = ScriptContext::m_pCurrent->GetParmDef(0);
		int maxAddons = varDef->ArraySize();
		char** names = (char**)parm[0]->Pointer;

		for(uint n = 0; n < addMan->GetNumAddons() && numAddons < maxAddons; n++)
		{
			ScriptContext::m_pCurrent->CopyString(addMan->GetAddon(n)->GetName().cstr(), &names[numAddons++], true);
		}
	}

	result->Int = numAddons;
}

//-----------------------------------------------------------------------
// proto int GetObjectMaterials(vobject object, string materials[])
//-----------------------------------------------------------------------
SCAPI(GetObjectMaterials)
{
	int numMaterials = 0;

	VObject* obj = (VObject*)parm[0]->Pointer;
	if(obj)
	{
		char** matNames = (char**)parm[1]->Pointer;
		const Variable* varDef = ScriptContext::m_pCurrent->GetParmDef(1);
		int maxMaterials = varDef->ArraySize();

		for(uint n = 0; n < obj->GetNumMaterials() && numMaterials < maxMaterials; n++)
		{
			const Material* mat = obj->GetMaterial(n);
			ScriptContext::m_pCurrent->CopyString(mat->GetName().cstr(), varDef, &matNames[numMaterials++]);
		}
	}

	result->Int = numMaterials;
}

//-----------------------------------------------------------------------
//proto int PlayVideo(widget w, int cmd)
//-----------------------------------------------------------------------
int NMETHOD PlayVideo(Widget* w, uint cmd)
{
	VideoWidget* videoWidget = VideoWidget::SafeCast(w);
	return videoWidget ? videoWidget->Play((VideoCommand)cmd) : 0;
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetImage(Widget* w, uint num)
{
	ImageWidget* imageWidget = ImageWidget::SafeCast(w);
	if(imageWidget)
		imageWidget->SetImage(num);
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetWorld(Widget* w, Entity* wrldEnt, uint cam)
{
	RenderTargetWidget* rt = RenderTargetWidget::SafeCast(w);
	rt->SetWorld(wrldEnt ? wrldEnt->GetWorld() : NULL, cam);
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetText(TextWidget* w, uint line, const char* text)
{
	AssertWidget(w);
	MultilineTextWidget* mw = MultilineTextWidget::SafeCast(w);
	if(mw)
		mw->SetText(line, text);
	else
		w->SetText(text);
}

//-----------------------------------------------------------------------
uint NMETHOD SetWidgetTextEx(TextWidget* w, uint line, uint fillmode, const char* text)
{
	AssertWidget(w);

	MultilineTextWidget* mw = MultilineTextWidget::SafeCast(w);
	if(mw)
		return mw->SetTextEx(line, text, fillmode);
	else
		w->SetTextEx(text);

	return 0;
}

//-----------------------------------------------------------------------
int NMETHOD SetActiveWindow(Widget* w, bool resetFocus)
{
	AssertWidget(w);

	enf::WidgetManager* wm = enf::GetWidgetManager();

	if(!wm)
		return 0;

	return wm->SetActiveWindow(w, resetFocus);
}

//-----------------------------------------------------------------------
void NMETHOD SetFocus(Widget* w)
{
	enf::WidgetManager* wm = enf::GetWidgetManager();

	if(!wm)
		return;

	wm->SetFocus(w);
}

//-----------------------------------------------------------------------
Widget* NMETHOD GetFocus()
{
	enf::WidgetManager* wm = enf::GetWidgetManager();

	if(!wm)
		return NULL;

	return wm->GetFocus();
}

//-----------------------------------------------------------------------
void NMETHOD SetModal(Widget* w)
{
	enf::WidgetManager* wm = enf::GetWidgetManager();

	if(!wm)
		return;

	wm->SetModal(w);
}

//-----------------------------------------------------------------------
uint NMETHOD AddListboxItem(TextListboxWidget* w, const char* text, uint column)
{
	AssertWidget(w);

	TextListboxWidget* lb = TextListboxWidget::SafeCast(w);
	if(!lb)
		return INDEX_NOT_FOUND;

	WString wtext;
	wtext = text;
	GetWidgetManager()->TranslateString(wtext, text, NULL);
	return lb->AddItem(wtext, NULL, column);
}

//-----------------------------------------------------------------------
void NMETHOD ClearListboxItems(TextListboxWidget* w)
{
	AssertWidget(w);

	TextListboxWidget* lb = TextListboxWidget::SafeCast(w);
	if(!lb)
		return;

	return lb->ClearItems();
}

//-----------------------------------------------------------------------
int NMETHOD GetListboxItemsCount(TextListboxWidget* w)
{
	AssertWidget(w);

	TextListboxWidget* lb = TextListboxWidget::SafeCast(w);
	if(!lb)
		return 0;

	return lb->GetNumItems();
}

//-----------------------------------------------------------------------
uint NMETHOD GetSelectedListboxRow(TextListboxWidget* w)
{
	AssertWidget(w);

	TextListboxWidget* lb = TextListboxWidget::SafeCast(w);
	if(!lb)
		return INDEX_NOT_FOUND;

	return lb->GetSelectedRow();
}

//-----------------------------------------------------------------------
void NMETHOD SelectListboxRow(TextListboxWidget* w, uint row)
{
	AssertWidget(w);

	TextListboxWidget* lb = TextListboxWidget::SafeCast(w);
	if(!lb)
		return;

	return lb->SetSelected(row, lb->GetSelectedColumn());
}

//-----------------------------------------------------------------------
void NMETHOD ShowWidget(Widget* w, uint show)
{
	if(w)
		w->Show(show != 0);
}

//-----------------------------------------------------------------------
void NMETHOD SetWidgetColor(Widget* w, uint color)
{
	if(w)
		w->SetColor(color);
}

//-----------------------------------------------------------------------
//proto native vector Vector(float x, float y, float z)
//-----------------------------------------------------------------------
Vector3 NMETHOD Vector(float x, float y, float z)
{
	return Vector3(x, y, z);
}

//-----------------------------------------------------------------------
//proto float IntersectRaySphere(vector raybase, vector raycos, vector center, float radius)
//-----------------------------------------------------------------------
SCAPI(IntersectRaySphere)
{
	float dist;
	if(IntersectionSphereRay(*(const Vector3*)parm[0]->Pointer, *(const Vector3*)parm[1]->Pointer, *(const Vector3*)parm[2]->Pointer, parm[3]->Float, &dist))
		result->Float = dist;
	else
		result->Float = -1;
}

//-----------------------------------------------------------------------
//proto float IntersectRayBox(vector start, vector end, vector mins, vector maxs)
//-----------------------------------------------------------------------
SCAPI(IntersectRayBox)
{
	float frac = 0.0f;

	if(IntersectionLineAABB(*(const Vector3*)parm[0]->Pointer, *(const Vector3*)parm[1]->Pointer, *(const Vector3*)parm[2]->Pointer, *(const Vector3*)parm[3]->Pointer, &frac))
	{
		result->Float = frac;
		return;
	}

	result->Float = -1;
}

//----------------------------------------------------------------------
//proto int CreateDecal(class ent, vector origin, vector project, float nearplane, float angle, float size, int shader, float lifetime, int flags)
//----------------------------------------------------------------------
Decal* NMETHOD CreateDecal(Entity* ent, const Vector3& origin, const Vector3& project, float nearplane,
			float angle, float size, const char* materialName, float lifetime, uint flags)
{
	Material* mat = GetMaterialSystem()->GetMaterial(materialName, MCF_DECAL);
	if(mat == NULL)
		return NULL;
	return BaseWorld::Current()->CreateDecal(AssertEntity(ent), origin, project, nearplane, angle, size, mat, lifetime, flags);
}

//----------------------------------------------------------------------
//proto void RemoveDecal(Decal dec)
//----------------------------------------------------------------------
void NMETHOD RemoveDecal(Decal* dec)
{
	if(dec != NULL)
		BaseWorld::Current()->DestroyDecal(dec);
}

//----------------------------------------------------------------------
//proto native hLMDecal CreateLandMarkDecal(_entity entity, vector origin, vector normal, float edgeSize, float lifeTime, int mat, hDecal prevDecal, float alpha)
//----------------------------------------------------------------------
void* NMETHOD CreateLandMarkDecal(Entity* ent, const Vector3& origin, const Vector3& normal, float edgeSize, float lifeTime, const char*  materialName, LandMarkDecal* prevDecal, float alpha)
{
	ComPtr<Material> mat = GetMaterialSystem()->GetMaterial(materialName, MCF_DECAL);

	if(mat == NULL)
		return NULL;

	return BaseWorld::Current()->CreateLandMarkDecal(
		AssertEntity(ent), origin, normal, edgeSize, lifeTime, mat, prevDecal, alpha);
}

//----------------------------------------------------------------------
//proto native int CanAddToLandMarkDecal(hdecal lmDecal, _entity entity, int mat, vector newPoint)
//----------------------------------------------------------------------
int NMETHOD CanAddToLandMarkDecal(LandMarkDecal* lmDecal, Entity* entity, const char* materialName, const Vector3& newPoint)
{
	if (lmDecal && lmDecal->GetType() == Decal::LANDMARK)
	{
		ComPtr<Material> mat = GetMaterialSystem()->GetMaterial(materialName, MCF_DECAL);

		if(mat == NULL)
			return LMD_ERROR;

		return lmDecal->IsValidForAdding(entity, mat, newPoint);
	}

	return LMD_ERROR;
}

//----------------------------------------------------------------------
//proto native bool AddPointToLandMarkDecal(hdecal lmDecal, vector point, vector normal, float alpha)
//----------------------------------------------------------------------
int NMETHOD AddPointToLandMarkDecal(LandMarkDecal* lmDecal, const Vector3& point, const Vector3& normal, float alpha)
{
	if (lmDecal && lmDecal->GetType() == Decal::LANDMARK)
	{
		return lmDecal->AddPoint(point, normal, alpha) ? 1 : 0;
	}

	return 0;
}

//----------------------------------------------------------------------
//proto native void FinalizeLandMarkDecal(hdecal lmDecal, bool addAlpha, float alphaDist)
//----------------------------------------------------------------------
void NMETHOD FinalizeLandMarkDecal(LandMarkDecal* lmDecal, int addAlpha, float alphaDist)
{
	if (lmDecal && lmDecal->GetType() == Decal::LANDMARK)
	{
		lmDecal->Finalize(addAlpha != 0, alphaDist);
	}
}

//----------------------------------------------------------------------
//proto native vector GetLastLandMarkPoint(hdecal lmDecal)
//----------------------------------------------------------------------
Vector3 NMETHOD GetLastLandMarkPoint(LandMarkDecal* lmDecal)
{
	if (lmDecal && lmDecal->GetType() == Decal::LANDMARK)
	{
		return lmDecal->GetLastPointWC();
	}

	return Vector3(-65535.0f);
}

//----------------------------------------------------------------------
//proto native bool IsLandMarkFinalized(hLMDecal lmDecal)
//----------------------------------------------------------------------
bool NMETHOD IsLandMarkFinalized(LandMarkDecal* lmDecal)
{
	if (lmDecal && lmDecal->GetType() == Decal::LANDMARK)
	{
		return lmDecal->IsFinalized();
	}

	return true;
}


//----------------------------------------------------------------------
//proto native void SetGlobalLandMarkParams(float minSegmentLength, float maxSegmentLength, float degAngle)
//----------------------------------------------------------------------
void NMETHOD SetGlobalLandMarkParams(float minSegmentLength, float maxSegmentLength, float degAngle)
{
	LandMarkDecal::SetGlobalParams(minSegmentLength, maxSegmentLength, degAngle);
}



//----------------------------------------------------------------------
//proto int TickCount()
//----------------------------------------------------------------------
uint NMETHOD TickCount(uint prev)
{
	return (uint)Timer::Sub(Timer::GetTicks(), prev);
}

//----------------------------------------------------------------------
//proto vobject CreateXOB(int nsurfaces, int nverts[], int numindices[], string shaders[])
//----------------------------------------------------------------------
SCAPI(CreateXOB)
{
	MeshObject* xob = ENF_NEW MeshObject();
	xob->Create((uint)parm[0]->Int, (uint*)parm[1]->Pointer, (uint*)parm[2]->Pointer, (const char**)parm[3]->Pointer, true);
	result->Pointer = xob;
}

//----------------------------------------------------------------------
//proto void UpdateVertsEx(class ent, uint surf, vector verts[], float uv[])
//----------------------------------------------------------------------
SCAPI(UpdateVertsEx)
{
	Entity* ent = (Entity*)parm[0]->InstancePtr;

	if(ent == NULL || ent->GetInstance() == NULL)
		return;

	MeshObject *xob = MeshObject::SafeCast(ent->GetVObject());
	if(xob == NULL)
		return;

	MeshLOD* lod = xob->GetLOD(0);
	uint mesh = parm[1]->Int;
	const Vector3* srcpos = (const Vector3*)parm[2]->Pointer;
	const float* srctc = (const float*)parm[3]->Pointer;

	if(lod->GetNumMeshes() < mesh)
		return;

	XPosition* pos = lod->LockMeshPositionBuffer(xob, mesh, 0, 0, false);
	XTexCoord* tc = lod->LockMeshTexCoordBuffer(mesh, 0, 0, false);
	uint numtc = lod->GetMeshNumTexCoords(mesh);

	for(uint n = 0; n < lod->GetMeshNumVertexes(mesh); n++)
	{
		pos[n].Pos = srcpos[n];
		tc[n * numtc].U = srctc[n * 2 + 0];
		tc[n * numtc].V = srctc[n * 2 + 1];
	}

	lod->UnlockMeshPositionBuffer(mesh);
	lod->UnlockMeshTexCoordBuffer(mesh);
	lod->UpdateMeshNormals(mesh);
	lod->UpdateMeshTangents(mesh);

	xob->UpdateBoundBox(ent);
}

//----------------------------------------------------------------------
//proto void UpdateIndices(vobject obj, uint surf, int indices[])
//----------------------------------------------------------------------
SCAPI(UpdateIndices)
{
	MeshObject *xob = MeshObject::SafeCast((VObjectResource*)parm[0]->Pointer);

	if(xob == NULL)
		return;

	MeshLOD* lod = xob->GetLOD(0);
	uint mesh = parm[1]->Int;

	if(mesh < lod->GetNumMeshes())
	{
		const uint* src = (const uint*)parm[2]->Pointer;

		uint faces = lod->GetMeshNumFaces(mesh);
		TMeshIndex* dest = lod->LockMeshIndexBuffer(mesh, 0, faces * 3, false);

		for(uint n = 0; n < faces * 3; n++)
			dest[n] = (TMeshIndex)src[n];

		lod->UnlockMeshIndexBuffer(mesh);
	}
}

//----------------------------------------------------------------------
//proto int PolyToTriangles(vector in[], int num, vector out1[])
//----------------------------------------------------------------------
SCAPI(PolyToTriangles)
{
	float *inp = (float*)parm[0]->Pointer;
	float *out = (float*)parm[2]->Pointer;
	int numpoints = parm[1]->Int;

	result->Int = PolyToTriangles(numpoints, (Vector3*)inp, (Vector3 *)out);
}

//----------------------------------------------------------------------
//proto int ClipPoly(vector in[], int num, vector out1[], int num1, vector out2[], int num2, float plane[4])
//----------------------------------------------------------------------
SCAPI(ClipPoly)
{

	Vector3 *inp = (Vector3*)parm[0]->Pointer;
	Vector3 *out1 = (Vector3*)parm[2]->Pointer;
	Vector3 *out2 = (Vector3*)parm[4]->Pointer;
	MPlane *plane = (MPlane*)parm[6]->Pointer;

	int numpoints = parm[1]->Int;

	Winding* w = Winding::Create(inp, numpoints);
	Winding* front;
	Winding* back;
	w->ClipEpsilon(plane->v, plane->w, 0, &front, &back);
	delete w;

	if(front)
	{
		CopyMem(out1, front->p, front->GetNumPoints() * sizeof(Vector3));
		parm[3]->Int = front->GetNumPoints();
		delete front;
	}
	if(back)
	{
		CopyMem(out2, back->p, back->GetNumPoints() * sizeof(Vector3));
		parm[5]->Int = front->GetNumPoints();
		delete back;
	}
}

//----------------------------------------------------------------------
//proto void SetPostEffect(int type, float parm0, float parm1, float parm2, float parm3)
//----------------------------------------------------------------------
/*void NMETHOD SetPostEffect(int type, float parm0, float parm1, float parm2, float parm3)
{
	return ;
	PostProcessEffect* pe = BaseWorld::Current()->GetPostprocessEffect(type);

	if(pe == NULL)
	{
		PostProcessManager* pp = (PostProcessManager*)GetEngine()->FindModule(PostProcessManagerTypeID);

		if(pp == NULL)
			return;

		switch(type)
		{
		case 0:
			pe = NULL; //this means nothing
			break;
		case 1:
			pe = pp->FindOrCreate("DeliriumEffect");
			break;
		case 2:
			pe = pp->FindOrCreate("MotionBlurEffect");
			break;
		case 3:
			pe = pp->FindOrCreate("RemapEffect");
			break;
		case 4:
			pe = pp->FindOrCreate("FishEyeEffect");
			break;
		case 5:
			pe = pp->FindOrCreate("RadialBlurEffect");
			break;
		case 6:
			pe = pp->FindOrCreate("BloomEffect");
			break;
		}
	}
	BaseWorld::Current()->SetPostprocessEffect(type, pe, 1, NULL);
	if(pe == NULL)
		return;

	pe->Set(0, &parm0);
	pe->Set(1, &parm1);
	pe->Set(2, &parm2);
	pe->Set(3, &parm3);
}
*/

void NMETHOD ResetInputDevice()
{
	GetInputDevice()->Reset(IMASK_KEYBOARD|IMASK_MOUSE|IMASK_JOYSTICK);
}

void NMETHOD MakeScreenshot(const char* name)
{
	VFile* vf = GetFileSystem()->GlobalOpen(name, FILEMODE_WRITE);
	if(vf)
	{
		CFileOutputStream	outstream(vf);
		g_pRender->MakeScreenshot(NULL, &outstream);
	}
}

void NMETHOD StartVideo(const char* name)
{
	if(!g_bGrabVideo)
	{
		if(GetEngine()->OpenVideo(name))
			g_bGrabVideo = true;
	}
}

void NMETHOD StopVideo()
{
	if(g_bGrabVideo)
	{
		GetEngine()->CloseVideo();
		g_bGrabVideo = false;
	}
}

void NMETHOD DiagRegisterMenu(int id, const char* name, const char* parent)
{
	enf_rlsassert2(id >= ENF_DIAG_USER, "Bad DiagMenu index!");
	ENF_DIAG_MENU(id, name, parent);
}

void NMETHOD DiagRegister(int id, const char* shortcut, const char* name, const char* parent, const char* values)
{
	enf_rlsassert2(id >= ENF_DIAG_USER, "Bad DiagMenu index!");
	ENF_DIAG_REGISTER(id, shortcut, name, parent, values);
}

void NMETHOD DiagUnregister(int id)
{
	enf_rlsassert2(id >= ENF_DIAG_USER, "Bad DiagMenu index!");
	ENF_DIAG_UNREGISTER(id);
}

void NMETHOD DiagRegisterBool(int id, const char* shortcut, const char* name, const char* parent, uint reverse)
{
	enf_rlsassert2(id >= ENF_DIAG_USER, "Bad DiagMenu index!");
	if(reverse)
		ENF_DIAG_REGISTER_RBOOL(id, shortcut, name, parent);
	else
		ENF_DIAG_REGISTER_BOOL(id, shortcut, name, parent);
}

uint NMETHOD DiagBool(int id, uint reverse)
{
	return reverse ? ENF_DIAG_RBOOL(id) : ENF_DIAG_BOOL(id);
}

uint NMETHOD DiagGetValue(int id)
{
	return ENF_DIAG_VALUE(id);
}

void NMETHOD DiagSetValue(int id, int value)
{
	enf_rlsassert2(id >= ENF_DIAG_USER, "Bad DiagMenu index!");
	ENF_DIAG_SET(id, value);
}

//----------------------------------------------------------------------
ScriptModule* NMETHOD LoadScript(ScriptModule* parentModule, const char* scriptFile, int listing)
{
	ScriptModule* module = g_pScript->CreateModule(scriptFile, parentModule, 256 * 1024);
	if(g_pScript->CompileModule(scriptFile, module, true))
	{
		return module;
	}

	module->Release();
	return NULL;
}

//-----------------------------------------------------------------------
uint NMETHOD SetSoundVolume(SoundHandle sound, float vol)
{
	if(GetSoundSystem() == NULL)
		return 0;

	return GetSoundSystem()->SetVolume(sound, vol);
}

//-----------------------------------------------------------------------
uint NMETHOD SetSoundFrequency(SoundHandle sound, uint freq)
{
	if(GetSoundSystem() == NULL)
		return 0;

	return GetSoundSystem()->SetFrequency(sound, freq);
}

//-----------------------------------------------------------------------
float NMETHOD GetSoundLength(SoundHandle sound)
{
	if(GetSoundSystem() == NULL)
		return 0;

	return GetSoundSystem()->GetLength(sound);
}

//-----------------------------------------------------------------------
float NMETHOD GetSoundPosition(SoundHandle sound)
{
	if(GetSoundSystem() == NULL)
		return 0;

	return GetSoundSystem()->GetPosition(sound);
}

//-----------------------------------------------------------------------
int NMETHOD EndSound(SoundHandle sound)
{
	if(GetSoundSystem() == NULL)
		return false;

	return (int)GetSoundSystem()->EndSound(sound);
}
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteBool(int value)
{
	m_pAdapter->WriteBool( value != 0 );
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteInt(int value)
{
	m_pAdapter->WriteInt32( value );
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteFloat(float value)
{
	m_pAdapter->WriteFloat( value );
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteString(const char* value)
{
	m_pAdapter->WriteString( value );
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteVector(const Vector3& value)
{
	m_pAdapter->WriteVector3( value );
}

//-----------------------------------------------------------------------
void NMETHOD PacketOutputAdapter::WriteMatrixAsQuaternionVector(const Matrix43& mat)
{
	if(BadVec(mat[0].v) ||
		BadVec(mat[1].v) ||
		BadVec(mat[2].v) ||
		BadVec(mat[3].v))
		g_pScript->Error("PacketOutputAdapter::WriteMatrixAsQuaternionVector() contains NAN");

	m_pAdapter->WriteMatrix43AsQuaternionVector(mat);
}


//-----------------------------------------------------------------------
int NMETHOD PacketInputAdapter::ReadBool() const
{
	return m_pAdapter->ReadBool() ? 1 : 0;
}

//-----------------------------------------------------------------------
int NMETHOD PacketInputAdapter::ReadInt() const
{
	return m_pAdapter->ReadInt32();
}

//-----------------------------------------------------------------------
float NMETHOD PacketInputAdapter::ReadFloat() const
{
	return m_pAdapter->ReadFloat();
}

//-----------------------------------------------------------------------------
SCAPI( PacketInputAdapter::ReadString )
{
	GetClass()->GetModule()->GetContext()->CopyString( m_pAdapter->ReadString(), &result->String, true );
}

//-----------------------------------------------------------------------
Vector3 NMETHOD PacketInputAdapter::ReadVector() const
{
	return m_pAdapter->ReadVector3();
}

//-----------------------------------------------------------------------
SCAPI( PacketInputAdapter::ReadMatrixAsQuaternionVector )
{
	const Variable* v = g_pScript->GetParmDef(0);

	if(v->GetVType() != VARTYPE_VECTOR || v->Spec & (VSPEC_CONST|VSPEC_PROTECT))
		g_pScript->Error(ScriptContext::RTE_WrongParameterValue);

	size_t posBefore = m_pAdapter->GetLength();
	Vector3 trans;
	Quaternion quat = m_pAdapter->ReadMatrix43AsQuaternionVector(trans);
	Matrix43 mat(quat, trans);

	size_t posAfter = m_pAdapter->GetLength();
	size_t length = posAfter - posBefore;

 	CopyMem(parm[0]->Pointer, &mat, sizeof(Vector3) * ((v->Dim[1] < 4) ? v->Dim[1] : 4));
}

#endif

//----------------------------------------------------------------------
//		Script extension functions
//----------------------------------------------------------------------
void EnforceScriptContext::Frame(BaseWorld *wrld)
{
//	LogF(LL_NORMAL, "====== FRAME =========");
#ifdef ENF_DONE
	if(wrld)
	{
		g_pvFTDelta->Float = (float)wrld->GetTimeSlice();
	}
	else
#endif
	{
		g_pvFTDelta->Float = (float)GetEngine()->GetTimeSlice();
	}
	if(g_pvFTDelta->Float <= 0)
		g_pvFTDelta->Float = 1;

#ifdef ENF_DONE
	if(GetInputDevice())
	{
		g_pvLMB->Int = ((GetInputDevice()->GetMouseButton(0) & 0x80000000) != 0);
		g_pvMMB->Int = ((GetInputDevice()->GetMouseButton(1) & 0x80000000) != 0);
		g_pvRMB->Int = ((GetInputDevice()->GetMouseButton(2) & 0x80000000) != 0);

		//float wheel;
		//GetInputDevice()->GetMouseState(&g_pvMouseDeltaX->Int, &g_pvMouseDeltaY->Int, &wheel);
		//g_pvMouseWheel->Int = wheel;
		GetInputDevice()->GetJoyLAxis(0, (float*)g_pvJoyAxis->Pointer, 3);
		GetInputDevice()->GetJoyRAxis(0, (float*)g_pvJoyRAxis->Pointer, 3);
		GetInputDevice()->GetJoySliders(0, (float*)g_pvJoySlider->Pointer, 4);
		ubyte jbuttons[16];
		int nbutt = GetInputDevice()->GetJoyButtons(0, jbuttons, 16);
		for(int n = 0; n < nbutt; n++)
			((uint*)g_pvJoyButtons->Pointer)[n] = jbuttons[n];
	}
#endif
}

//-----------------------------------------------------------------------
ErrorAction EnforceVMErrorHandler::HandleError(const char* description)
{
	enf::LogF(LL_ERROR, description);
	Error2Log(description);

	MsgBoxResult mbr = _MessageBox(MBT_ABORTRETRYIGNORE, "Virtual machine exception", description);
	switch(mbr)
	{
	case MBR_IDABORT:
		return EA_ABORT;
	case MBR_IDRETRY:
		return EA_RETRY;
	case MBR_IDIGNORE:
		return EA_IGNORE;
	}
	return EA_ABORT;
}

//-----------------------------------------------------------------------
EnforceScriptContext::EnforceScriptContext(StaticBuffer* stat): ScriptContext(stat)
{
	g_pScript = this;
	g_pStatic = stat;
	m_pEntityClass = NULL;
//	InitAPI();

	SetVMErrorHandler(&m_VMErrorHandler);
}

//-----------------------------------------------------------------------
void EnforceScriptContext::InitAPI()
{
	//user types
	// vobject values are ObjectResource* handles, so their script storage follows pointer width.
	g_vtVObjectType = AddType("vobject", VARTYPE_VOBJECT, sizeof(void*));
	g_vtSoundType = AddType("hsound", VARTYPE_HSOUND, sizeof(int));

	m_pEntityClass = ENF_NEW EntityClass(this, "_entity", sizeof(Entity), NULL);
	AddClass(m_pEntityClass, CF_FORWARD_DECLARATION);

#ifdef ENF_DONE
	Class* weclass = ENF_NEW ScriptedWidgetEventHandlerClass(this, "ScriptedWidgetEventHandler", sizeof(ScriptedWidgetEventHandler), NULL);
	AddClass(weclass, CF_FORWARD_DECLARATION);

	Widget::m_pClassPtr = ENF_NEW Class(this, "widget", sizeof(Widget), NULL);
	AddClass(Widget::m_pClassPtr, CF_FORWARD_DECLARATION);

#endif
	{
		ScriptModule::m_pClassPtr = ENF_NEW Class(this, "ScriptModule", sizeof(ScriptModule), NULL);
		AddClass(ScriptModule::m_pClassPtr, CF_FORWARD_DECLARATION);
		//special case - 'this' ScriptModule has been created prior to class creation
		m_pcClass = ScriptModule::m_pClassPtr;
		AddVariable("g_Script", ScriptModule::m_pClassPtr, 0, enf::VSPEC_PROTECT)->Pointer = this;
		//register ourselves manualy, as we were not existing during Instance constructor call
		RegisterInstance(this);
	}

//graphics settings
	g_pvWidth =			AddVariable("g_iWidth", m_vtIntType);
	g_pvHeight =		AddVariable("g_iHeight", m_vtIntType);
	
//runtime, per frame
	g_pvFTDelta =		AddVariable("ftime", m_vtFloatType, 0, VSPEC_PROTECT);

	g_pvViewFrame =	AddVariable("g_iViewFrame", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvIsOutdoor =	AddVariable("g_iIsOutdoor", m_vtIntType, 0, VSPEC_PROTECT);

//trace additional results
	g_pvTraceContent	= AddVariable("g_iTraceContent", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvTraceSurfparm	= AddVariable("g_iTraceSurfparm", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvTraceBone = AddVariable("g_iTraceBone", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvTraceShader = AddVariable("g_strTraceShader", m_vtStringType);

//Controls
	g_pvLMB =			AddVariable("LMB", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvRMB =			AddVariable("RMB", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvMMB =			AddVariable("MMB", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvMouseDeltaX =	AddVariable("MouseDeltaX", m_vtIntType);
	g_pvMouseDeltaY =	AddVariable("MouseDeltaY", m_vtIntType);
	g_pvMouseWheel =	AddVariable("MouseWheel", m_vtIntType, 0, VSPEC_PROTECT);
	g_pvJoyAxis =		AddVariable("JoyAxis", m_vtVectorType, 0, VSPEC_PROTECT);
	g_pvJoyRAxis =		AddVariable("JoyRAxis", m_vtVectorType, 0, VSPEC_PROTECT);
	g_pvJoySlider =	AddVariable("JoySliders", m_vtFloatType, 16, VSPEC_PTR|VSPEC_PROTECT);
	g_pvJoyButtons =	AddVariable("JoyButtons", m_vtIntType, 16, VSPEC_PTR|VSPEC_PROTECT);
	g_pvJoyPOV =		AddVariable("JoyPOV", m_vtIntType, 4, VSPEC_PTR|VSPEC_PROTECT);

	g_pvRenderShape = AddVariable("r_shape", m_vtIntType);
	g_pvRenderShape->Int = true;

	g_pvRenderParallax = AddVariable("r_parallax", m_vtIntType);
	g_pvRenderParallax->Int = true;

	g_pvRenderNormal = AddVariable("r_normal", m_vtIntType);
	g_pvRenderNormal->Int = true;

	g_pvRenderRTWidgets = AddVariable("r_display", m_vtIntType);
	g_pvRenderRTWidgets->Int = true;

	g_pvMaxPhysIterations = AddVariable("phys_MaxIterations", m_vtIntType);
	g_pvMaxPhysIterations->Int = 5;

	g_pvPhysFixedTick = AddVariable("phys_FixedTick", m_vtFloatType);
	g_pvPhysFixedTick->Float = 1.0f / 60.0f;

	AddVariable("FLT_MAX", m_vtFloatType, 0, VSPEC_CONST)->Float = FLT_MAX;
	AddVariable("FLT_MIN", m_vtFloatType, 0, VSPEC_CONST)->Float = FLT_MIN;

	ScriptModule* ctx = this;

	AddConst(ctx, MS_LB);
	AddConst(ctx, MS_MB);
	AddConst(ctx, MS_RB);
	AddConst(ctx, MS_X);
	AddConst(ctx, MS_Y);

//trace flags
	AddConst(ctx, TRACE_BONES);
	AddConst(ctx, TRACE_ENTS);
	AddConst(ctx, TRACE_LINE);
	AddConst(ctx, TRACE_WORLD);
	AddConst(ctx, TRACE_ONLY_PHYSICS);
	AddConst(ctx, TRACE_WATER);
	AddConst(ctx, TRACE_PASSTRANSLUCENT);
	AddConst(ctx, TRACE_RAGDOLLS);
	AddConst(ctx, TRACE_VISTEST);
	AddConst(ctx, TRACE_NOTRACE);
	AddConst(ctx, TRACE_TRANSPARENT_OCCLUDERS);

//widgets
#ifdef ENF_DONE
	AddConst(ctx, WidgetTypeID);
	AddConst(ctx, TextWidgetTypeID);
	AddConst(ctx, RenderTargetWidgetTypeID);
	AddConst(ctx, ImageWidgetTypeID);
	AddConst(ctx, ConsoleWidgetTypeID);
	AddConst(ctx, VideoWidgetTypeID);
	AddConst(ctx, RTTextureWidgetTypeID);
	AddConst(ctx, FrameWidgetTypeID);
	AddConst(ctx, PanelWidgetTypeID);
	AddConst(ctx, ButtonWidgetTypeID);
	AddConst(ctx, CheckBoxWidgetTypeID);
	AddConst(ctx, WindowWidgetTypeID);
	AddConst(ctx, ComboBoxWidgetTypeID);
	AddConst(ctx, SimpleProgressBarWidgetTypeID);
	AddConst(ctx, ProgressBarWidgetTypeID);
	AddConst(ctx, SliderWidgetTypeID);
	AddConst(ctx, BaseListboxWidgetTypeID);
	AddConst(ctx, TextListboxWidgetTypeID);
	AddConst(ctx, GenericListboxWidgetTypeID);
	AddConst(ctx, XComboBoxWidgetTypeID);
	AddConst(ctx, EditBoxWidgetTypeID);

	AddConst(ctx, WF_SOURCEALPHA);
	AddConst(ctx, WF_BLEND);
	AddConst(ctx, WF_ADDITIVE);
	AddConst(ctx, WF_VISIBLE);
	AddConst(ctx, WF_NOWRAP);
	AddConst(ctx, WF_CENTER);
	AddConst(ctx, WF_VCENTER);
	AddConst(ctx, WF_EXACTPOS);
	AddConst(ctx, WF_EXACTSIZE);
	AddConst(ctx, WF_NOFILTER);
	AddConst(ctx, WF_RALIGN);
	AddConst(ctx, WF_STRETCH);
	AddConst(ctx, WF_FLIPU);
	AddConst(ctx, WF_FLIPV);
	AddConst(ctx, WF_CUSTOMUV);
	AddConst(ctx, WF_IGNOREPOINTER);
	AddConst(ctx, WF_DISABLED);
	AddConst(ctx, WF_NOFOCUS);
	AddConst(ctx, WF_CLIPCHILDREN);
	AddConst(ctx, WF_RENDER_ALWAYS);
	AddConst(ctx, WF_NOCLEAR);

	AddConst(ctx, ALIGN_CENTER);
	AddConst(ctx, ALIGN_LEFT);
	AddConst(ctx, ALIGN_RIGHT);
	AddConst(ctx, ALIGN_TOP);
	AddConst(ctx, ALIGN_BOTTOM);

	AddConst(ctx, VP_PLAY);
	AddConst(ctx, VP_STOP);
	AddConst(ctx, VP_REWIND);
	AddConst(ctx, VP_POSITION);
	AddConst(ctx, VP_REPEAT);
	AddConst(ctx, VP_ISPLAYING);

//entity flags
	AddConst(ctx, TFL_VISIBLE);
	AddConst(ctx, TFL_SOLID);
	AddConst(ctx, TFL_ACTIVE);
	AddConst(ctx, TFL_TRIGGER);
	AddConst(ctx, TFL_AREASPLITTER);
	AddConst(ctx, TFL_AREACLOSED);
	AddConst(ctx, TFL_USER1);
	AddConst(ctx, TFL_USER2);
	AddConst(ctx, TFL_USER6);
	AddConst(ctx, TFL_TRANSLUCENT);
	AddConst(ctx, TFL_STATIC);
	AddConst(ctx, TFL_ACTIVE);

//events
	AddConst(ctx, EV_TOUCH);
	AddConst(ctx, EV_EXTRA);
	AddConst(ctx, EV_INIT);
	AddConst(ctx, EV_VISIBLE);
	AddConst(ctx, EV_NOTVISIBLE);
	AddConst(ctx, EV_FRAME);
	AddConst(ctx, EV_POSTFRAME);
	AddConst(ctx, EV_TICK);
	AddConst(ctx, EV_WORLDPROCESS);
	AddConst(ctx, EV_FLASHLIGHT);
	AddConst(ctx, EV_ANIMEND);
	AddConst(ctx, EV_ANIMBLEND);
	AddConst(ctx, EV_SOUNDEND);
	AddConst(ctx, EV_JOINTBREAK);
	AddConst(ctx, EV_PHYSICSMOVE);
	AddConst(ctx, EV_CONTACT);
	AddConst(ctx, EV_SIMULATE);
	AddConst(ctx, EV_ALL);

//SetAnimSlot
	AddConst(ctx, AF_ONCE);
	AddConst(ctx, AF_USER);
	AddConst(ctx, AF_RESET);
	AddConst(ctx, AF_FORCEFPS);
	AddConst(ctx, AF_BLENDOUT);
	AddConst(ctx, AF_NOANIMEND);
	AddConst(ctx, AF_NOANIMHOOKS);

//light flags
	AddConst(ctx, LFL_CHEAP);
	AddConst(ctx, LFL_DYNAMIC);
	AddConst(ctx, LFL_CASTSHADOW);
	AddConst(ctx, LFL_FLASHLIGHT);
//light types
	AddConst(ctx, LT_AMBIENT);
	AddConst(ctx, LT_POINT);
	AddConst(ctx, LT_SPOT);
	AddConst(ctx, LT_DIRECTIONAL);

//surfparms
	AddConst(ctx, SURF_NOIMPACT);
	AddConst(ctx, SURF_NOMARKS);

//footsteps
	AddConst(ctx, SURF_DUST);
	{
		const uint SURF_MATERIAL_MASK = SurfaceParms::SURF_MATERIAL_MASK;
		const uint SURF_MATERIAL_SHIFT = SurfaceParms::SURF_MATERIAL_SHIFT;
		AddConst(ctx, SURF_MATERIAL_MASK);
		AddConst(ctx, SURF_MATERIAL_SHIFT);
	}
	AddConst(ctx, C_SOLID);
	AddConst(ctx, C_DETAIL);
	AddConst(ctx, C_WATER);
	AddConst(ctx, C_TRANSLUCENT);
	AddConst(ctx, C_LEAF);

//files
	AddConst(ctx, FILEMODE_WRITE);
	AddConst(ctx, FILEMODE_READ);

	AddConst(ctx, PITCH);
	AddConst(ctx, YAW);
	AddConst(ctx, ROLL);
	AddConst(ctx, true);
	AddConst(ctx, false);
	AddConst(ctx, NULL);

	AddConst(ctx, SHAPE_BBOX);
	AddConst(ctx, SHAPE_DIAMOND);
	AddConst(ctx, SHAPE_PYRAMID);
	AddConst(ctx, SHAPE_LINE);
	AddConst(ctx, SHAPE_CYLINDER);
	AddConst(ctx, SHAPE_SPHERE);

	AddConst(ctx, SS_NOZBUFFER);
	AddConst(ctx, SS_NOZWRITE);
	#define SS_NOZUPDATE SS_NOZWRITE
	AddConst(ctx, SS_NOZUPDATE);
	AddConst(ctx, SS_DOUBLESIDE);
	AddConst(ctx, SS_WIREFRAME);
	AddConst(ctx, SS_TRANSP);
	AddConst(ctx, SS_ONCE);
	AddConst(ctx, SS_NOOUTLINE);
	AddConst(ctx, SS_NOCULL);
	AddConst(ctx, SS_VISIBLE);

	AddConst(ctx, EP_CONEANGLE);
	AddConst(ctx, EP_EMITOFFSET);
	AddConst(ctx, EP_VELOCITY);
	AddConst(ctx, EP_VELOCITY_RND);
	AddConst(ctx, EP_AVELOCITY);
	AddConst(ctx, EP_SIZE);
	AddConst(ctx, EP_STRETCH);
	AddConst(ctx, EP_RANDOM_ANGLE);
	AddConst(ctx, EP_RANDOM_ROT);
	AddConst(ctx, EP_AIR_RESISTANCE);
	AddConst(ctx, EP_AIR_RESISTANCE_RND);
	AddConst(ctx, EP_GRAVITY_SCALE);
	AddConst(ctx, EP_GRAVITY_SCALE_RND);
	AddConst(ctx, EP_BIRTH_RATE);
	AddConst(ctx, EP_BIRTH_RATE_RND);
	AddConst(ctx, EP_LIFETIME);
	AddConst(ctx, EP_LIFETIME_RND);
	AddConst(ctx, EP_LIFETIME_BY_ANIM);
	AddConst(ctx, EP_ANIM_ONCE);
	AddConst(ctx, EP_EFFECT_TIME);
	AddConst(ctx, EP_REPEAT);
	AddConst(ctx, EP_SORT);
	AddConst(ctx, EP_CURRENT_TIME);
	AddConst(ctx, EP_ACTIVE_PARTICLES);

	AddConst(ctx, AS_ACTIVE);
	AddConst(ctx, AS_INACTIVE);
	AddConst(ctx, AS_ALWAYS_ACTIVE);

	AddConst(ctx, LMD_VALID);
	AddConst(ctx, LMD_TOO_FAR);
	AddConst(ctx, LMD_ERROR);
	AddConst(ctx, LMD_DIFF_ENT);

	AddConstF(ctx, TICK_PERIOD);

	AddConst(ctx, SFX_ONCE);
	AddConst(ctx, SFX_MUSIC);
	AddConst(ctx, SFX_AMBIENT);
	AddConst(ctx, SFX_3D);
	AddConst(ctx, SFX_DISCARDABLE);
	AddConst(ctx, SFX_DIRECTIONAL);
	AddConst(ctx, SFX_DOPPLER);
	AddConst(ctx, SFX_STATIC);
	AddConst(ctx, SFX_NOTEST);

	AddConst(ctx, SOUND_NO_SHADER);
	AddConst(ctx, SOUND_NOT_PLAYING);

	AddConst(ctx, FILEATTR_DIRECTORY);
	AddConst(ctx, FILEATTR_HIDDEN);
	AddConst(ctx, FILEATTR_READONLY);
	AddConst(ctx, FILEATTR_INVALID);

//camera types
	AddConst(ctx, PT_PERSPECTIVE);
	AddConst(ctx, PT_ORTHOGRAPHIC);
#endif
}


//-----------------------------------------------------------------------
void EnforceScriptContext::AfterCompile()
{
	ScriptContext::AfterCompile();

#ifdef ENF_DONE
	RegMethod2( ScriptModule, ScriptModule, Release, _Release, this);

	RegFunct2(LoadScript, API::LoadScript, this);
	RegFunct2(MakeScreenshot, API::MakeScreenshot, this);
	RegFunct2(StartVideo, API::StartVideo, this);
	RegFunct2(StopVideo, API::StopVideo, this);
	RegSFunct2(Throw, API::Throw, this);
	RegSFunct2(DumpStack, API::DumpStack, this);
	RegSFunct2(Print, API::Print, this);
	RegSFunct2(DPrint, API::DPrint, this);
	RegFunct2(Error2, API::Error2, this);
	RegSFunct2(vtoa, API::vtoa, this);
	RegFunct2(GetWorldTime, API::GetWorldTime, this);
	RegFunct2(SetCurrentMap, API::SetCurrentMap, this);
	RegFunct2(UpdateCubeMap, API::UpdateCubeMap, this);
	RegSFunct2(AddLight, API::AddLight, this);
	RegFunct2(RemoveLight, API::RemoveLight, this);
	RegFunct2(SetLightFlags, API::SetLightFlags, this);
	RegFunct2(ClearLightFlags, API::ClearLightFlags, this);
	RegFunct2(SetLightEx, API::SetLightEx, this);
	RegFunct2(SetLightCone, API::SetLightCone, this);
	RegFunct2(SetLightTexture, API::SetLightTexture, this);
	RegFunct2(AddChild, API::AddChild, this);
	RegFunct2(RemoveChild, API::RemoveChild, this);
	RegFunct2(ClearFlags, API::ClearFlags, this);
	RegFunct2(GetOrigin, API::GetOrigin, this);
	RegFunct2(GetFlags, API::GetFlags, this);
	RegFunct2(GetAngles, API::GetAngles, this);
	RegFunct2(GetLocalAngles, API::GetLocalAngles, this);
	RegFunct2(GetMatrix, API::GetMatrix, this);
	RegSFunct2(GetMatrix4, API::GetMatrix4, this);
	RegFunct2(SetFlags, API::SetFlags, this);
	RegFunct2(SetRenderViewMask, API::SetRenderViewMask, this);
	RegFunct2(SetEventMask, API::SetEventMask, this);
	RegFunct2(GetEventMask, API::GetEventMask, this);
	RegFunct2(ClearEventMask, API::ClearEventMask, this);
	RegFunct2(SetOrigin, API::SetOrigin, this);
	RegFunct2(SetMatrix, API::SetMatrix, this);
	RegFunct2(SetWeight, API::SetWeight, this);
	RegFunct2(SetAngles, API::SetAngles, this);
	RegFunct2(SetFixedLOD, API::SetFixedLOD, this);
	RegSFunct2(CreateModel, API::CreateModel, this);
	RegSFunct2(RemoveModel, API::RemoveModel, this);
	RegSFunct2(GetBoundBox, API::GetBoundBox, this);
	RegSFunct2(GetCBoundBox, API::GetCBoundBox, this);
	RegFunct2(GetNextEntity, API::GetNextEntity, this);
	RegFunct2(GetNumActiveEntities, API::GetNumActiveEntities, this);
	RegFunct2(GetActiveEntity, API::GetActiveEntity, this);
	RegFunct2(UpdateEntity, API::UpdateEntity, this);
	RegFunct2(TraceLineToEntity, API::TraceLineToEntity, this);
	RegSFunct2(TraceLine, API::TraceLine, this);
	RegSFunct2(TraceLineEx, API::TraceLineEx, this);
	RegFunct2(P2PVisibilityEx, API::P2PVisibilityEx, this);
	RegSFunct2(VisEntities, API::VisEntities, this);
	RegSFunct2(IntersectRaySphere, API::IntersectRaySphere, this);
	RegSFunct2(IntersectRayBox, API::IntersectRayBox, this);
	RegSFunct2(SphereQuery, API::SphereQuery, this);
	RegFunct2(IsBoxVisible, API::IsBoxVisible, this);
	RegFunct2(KeyState, API::KeyState, this);
	RegFunct2(MouseState, API::MouseState, this);
	RegFunct2(GetMousePos, API::GetMousePos, this);
	RegFunct2(GetScreenSize, API::GetScreenSize, this);
	RegFunct2(GetWidgetUnderCursor, API::GetWidgetUnderCursor, this);
	RegFunct2(ClearKey, API::ClearKey, this);
	RegFunct2(SetCamera, API::SetCamera, this);
	RegFunct2(SetCameraEx, API::SetCameraEx, this);
	RegFunct2(GetCamera, API::GetCamera, this);
	RegFunct2(SetListenerCamera, API::SetListenerCamera, this);
	RegFunct2(SetCameraVerticalFOV, API::SetCameraVerticalFOV, this);
	RegFunct2(SetCameraFarPlane, API::SetCameraFarPlane, this);
	RegFunct2(SetCameraNearPlane, API::SetCameraNearPlane, this);
	RegFunct2(SetCameraPostProcessEffect, API::SetCameraPostProcessEffect, this);
	RegFunct2(SetCameraType, API::SetCameraType, this);
	RegFunct2(BumpFrame, API::BumpFrame, this);
	RegFunct2(BumpFrameEx, API::BumpFrameEx, this);
	RegFunct2(SetGUIWidget, API::SetGUIWidget, this);
	RegFunct2(SetRenderView, API::SetRenderView, this);
	RegFunct2(GetRenderView, API::GetRenderView, this);
	RegFunct2(SetBone, API::SetBone, this);
	RegFunct2(SetBoneMatrix, API::SetBoneMatrix, this);
	RegFunct2(SetBoneGlobal, API::SetBoneGlobal, this);
	RegFunct2(GetBoneMatrix, API::GetBoneMatrix, this);
	RegFunct2(GetBoneLocalMatrix, API::GetBoneLocalMatrix, this);
	RegFunct2(SetFrame, API::SetFrame, this);
	RegFunct2(SetAnimSlot, API::SetAnimSlot, this);
	RegFunct2(IsAnimSlotPlaying, API::IsAnimSlotPlaying, this);
	RegFunct2(SetAnimMask, API::SetAnimMask, this);
	RegFunct2(SetMorphState, API::SetMorphState, this);
	RegFunct2(ChangeAnimSlotFPS, API::ChangeAnimSlotFPS, this);
	RegFunct2(ChangeAnimSlotMask, API::ChangeAnimSlotMask, this);

	RegFunct2(ClearAnimMask, API::ClearAnimMask, this);
	RegFunct2(GetObject, API::GetObject, this);
	RegSFunct2(GetObjectMaterials, API::GetObjectMaterials, this);
	RegFunct2(ReleaseObject, API::ReleaseObject, this);
	RegFunct2(SelectObject, API::SelectObject, this);
	RegSFunct2(VectorNormalize, API::VectorNormalize, this);
	RegFunct2(VectorLength, API::VectorLength, this);
	RegFunct2(VectorLengthSq, API::VectorLengthSq, this);
	RegSFunct2(Vector2Angles, API::Vector2Angles, this);
	RegSFunct2(Vector2Yaw, API::Vector2Yaw, this);
	RegFunct2(Yaw2Vector, API::Yaw2Vector, this);
	RegSFunct2(VVector2Angles, API::VVector2Angles, this);
	RegSFunct2(Angles2Vector, API::Angles2Vector, this);
	RegSFunct2(VectorMatrixMultiply4, API::VectorMatrixMultiply4, this);
	RegSFunct2(VectorMatrixMultiply3, API::VectorMatrixMultiply3, this);
	RegSFunct2(VectorMatrixInvMultiply4, API::VectorMatrixInvMultiply4, this);
	RegSFunct2(VectorMatrixInvMultiply3, API::VectorMatrixInvMultiply3, this);
	RegSFunct2(MatrixMultiply3, API::MatrixMultiply3, this);
	RegSFunct2(MatrixMultiply4, API::MatrixMultiply4, this);
	RegSFunct2(MatrixInvMultiply3, API::MatrixInvMultiply3, this);
	RegSFunct2(MatrixInvMultiply4, API::MatrixInvMultiply4, this);
	RegSFunct2(RollPitchYawMatrix, API::RollPitchYawMatrix, this);
	RegSFunct2(ProjectVector, API::ProjectVector, this);
	RegSFunct2(UnprojectVector, API::UnprojectVector, this);
	RegSFunct2(AroundVectorMatrix, API::AroundVectorMatrix, this);
	RegSFunct2(MatrixAngles, API::MatrixAngles, this);
	RegSFunct2(QuaternionAngles, API::QuaternionAngles, this);
	RegSFunct2(QuatMatrix, API::QuatMatrix, this);
	RegSFunct2(MatrixQuat, API::MatrixQuat, this);
	RegSFunct2(QuatLerp, API::QuatLerp, this);
	RegSFunct2(QuatMultiply, API::QuatMultiply, this);
	RegFunct2(Vector, API::Vector, this);
	RegFunct2(CreateWidget, API::CreateWidget, this);
	RegFunct2(CreateWidgetEx, API::CreateWidgetEx, this);
	RegFunct2(CreateWidgets, API::CreateWidgets, this);
	RegFunct2(SetCursorWidget, API::SetCursorWidget, this);
	RegFunct2(ShowCursorWidget, API::ShowCursorWidget, this);
	RegFunct2(ReportMouse, API::ReportMouse, this);

	RegFunct2(LoadWidgetImageSet, API::LoadWidgetImageSet, this);
	RegFunct2(LoadWidgetStyles, API::LoadWidgetStyles, this);

	#ifdef ENF_PS3
		RegFunct2(IsCircleToCrossSwapped, API::IsCircleToCrossSwapped, this);
	#endif

	RegFunct2(FindWidget, API::FindWidget, this);
	RegFunct2(FindAnyWidget, API::FindAnyWidget, this);
	RegFunct2(SetWidgetEventHandler, API::SetWidgetEventHandler, this);
	
	RegFunct2(PlayVideo, API::PlayVideo, this);
	RegFunct2(SetWidgetPos, API::SetWidgetPos, this);
	RegFunct2(SetWidgetUV, API::SetWidgetUV, this);
	RegMethod2(TextWidget, TextWidget, SetTextOffset, SetTextOffset, this);
	RegMethod2(TextWidget, TextWidget, SetTextSpacing, SetTextSpacing, this);
	RegMethod2(ImageWidget, ImageWidget, SetImageTexture, _SetImageTexture, this);
	RegFunct2(DestroyWidget, API::DestroyWidget, this);
	RegFunct2(SetWidgetColor, API::SetWidgetColor, this);
	RegFunct2(ShowWidget, API::ShowWidget, this);
	RegFunct2(LoadWidgetImage, API::LoadWidgetImage, this);
	RegFunct2(UnloadWidgetImage, API::UnloadWidgetImage, this);
	RegFunct2(ReloadTexture, API::ReloadTexture, this);
	RegFunct2(SetWidgetImage, API::SetWidgetImage, this);
	RegFunct2(SetWidgetWorld, API::SetWidgetWorld, this);
	RegFunct2(SetWidgetText, API::SetWidgetText, this);
	RegFunct2(SetWidgetTextEx, API::SetWidgetTextEx, this);
	RegFunct2(SetActiveWindow, API::SetActiveWindow, this);
	RegFunct2(SetFocus, API::SetFocus, this);
	RegFunct2(GetFocus, API::GetFocus, this);
	RegFunct2(SetModal, API::SetModal, this);
	RegFunct2(CreateFont, API::CreateFont, this);
	RegFunct2(SetFont, API::SetFont, this);
	RegFunct2(BeginParse, API::BeginParse, this);
	RegSFunct2(ParseLine, API::ParseLine, this);
	RegFunct2(EndParse, API::EndParse, this);
	RegSFunct2(FileExist, API::FileExist, this);
	RegSFunct2(OpenFile, API::OpenFile, this);
	RegSFunct2(ReadFile, API::ReadFile, this);
	RegSFunct2(CloseFile, API::CloseFile, this);
	RegSFunct2(FPrint, API::FPrint, this);
	RegSFunct2(FGets, API::FGets, this);
	RegSFunct2(FindFile, API::FindFile, this);
	RegFunct2(MakeDirectory, API::MakeDirectory, this);
	RegFunct2(DeleteFile, API::DeleteFile, this);
	RegFunct2(CopyFile, API::CopyFile, this);
	RegSFunct2(FindNextFile, API::FindNextFile, this);
	RegFunct2(CloseFindFile, API::CloseFindFile, this);
	RegSFunct2(FPrintln, API::FPrintln, this);
	RegFunct2(AddDCylinder, API::AddDCylinder, this);
	RegFunct2(AddDSphere, API::AddDSphere, this);
	RegFunct2(AddDShape, API::AddDShape, this);
	RegFunct2(AddDLines, API::AddDLines, this);
	RegFunct2(AddDTris, API::AddDTris, this);

	RegMethod2( widget, Widget, GetParent, _GetParent, this);
	RegMethod2( widget, Widget, GetName, _GetName, this);
	RegMethod2( widget, Widget, GetTypeName, _GetTypeName, this);
	RegMethod2( widget, Widget, GetTypeID, _GetTypeID, this);
	RegMethod2( widget, Widget, Show, _Show, this);
	RegMethod2( widget, Widget, Enable, _Enable, this);
	RegMethod2( widget, Widget, GetFlags, _GetFlags, this);
	RegMethod2( widget, Widget, SetFlags, _SetFlags, this);
	RegMethod( widget, Widget, ClearFlags, this);
	RegMethod2( widget, Widget, IsControlClass, _IsControlClass, this);
	RegMethod2( widget, Widget, GetStyleName, _GetStyleName, this);
	RegMethod( widget, Widget, GetUserID, this);
	RegMethod( widget, Widget, SetUserID, this);
	RegMethod2( widget, Widget, GetPos, _GetPos, this );
	RegMethod2( widget, Widget, GetSize, _GetSize, this );
	RegMethod2( widget, Widget, GetScreenPos, _GetScreenPos, this );
	RegMethod2( widget, Widget, GetScreenSize, _GetScreenSize, this );
	RegMethod2( widget, Widget, SetTransform, SetTransform, this );
	RegMethod2( widget, Widget, IsVisible, _IsVisible, this);
	RegMethod2( widget, Widget, IsVisibleHierarchy, _IsVisibleHierarchy, this);
	RegMethod2( widget, Widget, SetPos, _SetPos, this);
	RegMethod2( widget, Widget, SetSize, _SetSize, this);

	RegMethod2( BaseListboxWidget, BaseListboxWidget, ClearItems, _ClearItems, this);
	RegMethod2( BaseListboxWidget, BaseListboxWidget, GetNumItems, _GetNumItems, this);
	RegMethod( BaseListboxWidget, BaseListboxWidget, SelectRow, this);
	RegMethod( BaseListboxWidget, BaseListboxWidget, GetSelectedRow, this);
	RegMethod2( TextListboxWidget, TextListboxWidget, AddItem, _AddItem, this);
	RegMethod2( TextListboxWidget, TextListboxWidget, SetItem, _SetItem, this);
	RegMethod2( TextListboxWidget, TextListboxWidget, GetItemData, _GetItemData, this);

	RegMethod( RenderTargetWidget, RenderTargetWidget, SetRefresh, this);
	RegMethod( RenderTargetWidget, RenderTargetWidget, SetResolutionScale, this);

	RegFunct2(AddListboxItem, API::AddListboxItem, this);
	RegFunct2(ClearListboxItems, API::ClearListboxItems, this);
	RegFunct2(GetListboxItemsCount, API::GetListboxItemsCount, this);
	RegFunct2(GetSelectedListboxRow, API::GetSelectedListboxRow, this);
	RegFunct2(SelectListboxRow, API::SelectListboxRow, this);

	RegMethod(XComboBoxWidget, XComboBoxWidget, AddItem, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, ClearAll, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, SetItem, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, RemoveItem, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, GetNumItems, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, SetCurrentItem, this);
	RegMethod(XComboBoxWidget, XComboBoxWidget, GetCurrentItem, this);

	RegMethod(SliderWidget, SliderWidget, GetMin, this);
	RegMethod(SliderWidget, SliderWidget, GetMax, this);
	RegMethod(SliderWidget, SliderWidget, GetCurrent, this);
	RegMethod(SliderWidget, SliderWidget, SetCurrent, this);

	RegMethod(SimpleProgressBarWidget, SimpleProgressBarWidget, GetMin, this);
	RegMethod(SimpleProgressBarWidget, SimpleProgressBarWidget, GetMax, this);
	RegMethod(SimpleProgressBarWidget, SimpleProgressBarWidget, GetCurrent, this);
	RegMethod(SimpleProgressBarWidget, SimpleProgressBarWidget, SetCurrent, this);

	RegMethod2(EditBoxWidget, EditBoxWidget, SetText, SetText_, this);
	RegMethod2(EditBoxWidget, EditBoxWidget, GetText, GetText_, this);

	RegMethod2(CheckBoxWidget, CheckBoxWidget, IsChecked, IsChecked_, this);
	RegMethod2(CheckBoxWidget, CheckBoxWidget, SetChecked, SetChecked_, this);
	RegMethod(CheckBoxWidget, CheckBoxWidget, SetText, this);

	RegMethod(ButtonWidget, ButtonWidget, SetText, this);
	RegMethod(ButtonWidget, ButtonWidget, SetTextOffset, this);
	RegMethod(ButtonWidget, ButtonWidget, SetHorizontalAlignment, this);
	RegMethod(ButtonWidget, ButtonWidget, SetVerticalAlignment, this);
	RegMethod2(ButtonWidget, ButtonWidget, SetState, _SetState, this);
	RegMethod2(ButtonWidget, ButtonWidget, GetState, _GetState, this);

	RegMethod(Shape, Shape, Destroy, this);
	RegMethod(Shape, Shape, SetColor, this);
	RegMethod(Shape, Shape, SetMatrix, this);
	RegMethod(Shape, Shape, SetFlags, this);

	RegFunct2(CreateDecal, API::CreateDecal, this);
	RegFunct2(RemoveDecal, API::RemoveDecal, this);

	RegFunct2(CreateLandMarkDecal, API::CreateLandMarkDecal, this);
	RegFunct2(CanAddToLandMarkDecal, API::CanAddToLandMarkDecal, this);
	RegFunct2(AddPointToLandMarkDecal, API::AddPointToLandMarkDecal, this);
	RegFunct2(FinalizeLandMarkDecal, API::FinalizeLandMarkDecal, this);
	RegFunct2(GetLastLandMarkPoint, API::GetLastLandMarkPoint, this);
	RegFunct2(IsLandMarkFinalized, API::IsLandMarkFinalized, this);
	RegFunct2(SetGlobalLandMarkParams, API::SetGlobalLandMarkParams, this);

	RegFunct2(TickCount, API::TickCount, this);
	
	RegSFunct2(CreateXOB, API::CreateXOB, this);
	RegSFunct2(UpdateVertsEx, API::UpdateVertsEx, this);
	RegSFunct2(UpdateIndices, API::UpdateIndices, this);

	RegSFunct2(ClipPoly, API::ClipPoly, this);
	RegSFunct2(PolyToTriangles, API::PolyToTriangles, this);
	RegSFunct2(SetParticleParm, API::SetParticleParm, this);
	RegSFunct2(GetParticleParm, API::GetParticleParm, this);
	RegSFunct2(GetParticleParmOriginal, API::GetParticleParmOriginal, this);
	RegSFunct2(GetParticleEmitors, API::GetParticleEmitors, this);
	RegFunct2(ResetParticlePosition, API::ResetParticlePosition, this);
	RegFunct2(GetParticleCount, API::GetParticleCount, this);

	RegFunct2(SetSoundVolume, API::SetSoundVolume, this);
	RegFunct2(SetSoundFrequency, API::SetSoundFrequency, this);
	RegFunct2(GetSoundLength, API::GetSoundLength, this);
	RegFunct2(GetSoundPosition, API::GetSoundPosition, this);
	RegFunct2(EndSound, API::EndSound, this);

	RegFunct2(ResetInputDevice, API::ResetInputDevice, this);

	RegSFunct2(AddNamedEntity, API::AddNamedEntity, this);
	RegFunct2(RemoveNamedEntity, API::RemoveNamedEntity, this);
	RegFunct2(FindNamedEntity, API::FindNamedEntity, this);
	RegFunct2(FindEntityByID, API::FindEntityByID, this);

	RegMethod(widget, Widget, SetColor, this);

	RegMethod2(_entity, Entity, VectorToParent, _VectorToParent, this);
	RegMethod2(_entity, Entity, CoordToParent, _CoordToParent, this);
	RegMethod2(_entity, Entity, VectorToLocal, _VectorToLocal, this);
	RegMethod2(_entity, Entity, CoordToLocal, _CoordToLocal, this);

	RegMethod(_entity, Entity, GetID, this);
	RegMethod(_entity, Entity, SetID, this);
	RegMethod(_entity, Entity, GetParent, this);
	RegMethod(_entity, Entity, GetChildren, this);
	RegMethod(_entity, Entity, GetSibling, this);
	RegMethod(_entity, Entity, SetObject, this);
	RegMethod(_entity, Entity, GetVObject, this);
	RegMethod(_entity, Entity, SetTick, this);
	RegMethod(_entity, Entity, RemoveTick, this);

	RegFunct2(CreateOcclusionQuery, API::CreateOcclusionQuery, this);
	RegMethod(OcclusionQuery, OcclusionQuery, GetResult, this);
	RegMethod(OcclusionQuery, OcclusionQuery, Destroy, this);
	RegMethod(OcclusionQuery, OcclusionQuery, SetPosition, this);

	Funct *fn;

	if((fn = GetFunction("itoa")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(32, NULL, __FILE__, __LINE__);

	if((fn = GetFunction("tostr")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(4, NULL, __FILE__, __LINE__);

	if((fn = GetFunction("ftoa")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(32, NULL, __FILE__, __LINE__);

	if((fn = GetFunction("vtoa")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(256, NULL, __FILE__, __LINE__);

	if((fn = GetFunction("substr")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(8192, NULL, __FILE__, __LINE__);

	if((fn = GetFunction("trim")) != NULL && fn->GetReturnVariable())
		fn->GetReturnVariable()->Pointer = AllocMem(8192, NULL, __FILE__, __LINE__);

	RegFunct2(DiagRegisterMenu, API::DiagRegisterMenu, this);
	RegFunct2(DiagRegister, API::DiagRegister, this);
	RegFunct2(DiagUnregister, API::DiagUnregister, this);
	RegFunct2(DiagRegisterBool, API::DiagRegisterBool, this);
	RegFunct2(DiagBool, API::DiagBool, this);
	RegFunct2(DiagGetValue, API::DiagGetValue, this);
	RegFunct2(DiagSetValue, API::DiagSetValue, this);

	RegFunct2(SetSkyUserPlanets, API::SetSkyUserPlanets, this);
	RegFunct2(SetSkyPlanet, API::SetSkyPlanet, this);
	RegFunct2(SetSkyPlanetSize, API::SetSkyPlanetSize, this);
	RegFunct2(LoadSkyPresets, API::LoadSkyPresets, this);
	RegFunct2(InitSky, API::InitSky, this);
	RegFunct2(SetSkyPreset, API::SetSkyPreset, this);
	RegFunct2(LerpSkyPreset, API::LerpSkyPreset, this);
	RegFunct2(LerpSkyPreset3, API::LerpSkyPreset3, this);
	RegFunct2(SetStarsObserverPosition, API::SetStarsObserverPosition, this);
	RegFunct2(SetStarsObserverTime, API::SetStarsObserverTime, this);
	RegFunct2(SetNightLayerRotMatrix, API::SetNightLayerRotMatrix, this);
	RegFunct2(SetStarsRotMatrix, API::SetStarsRotMatrix, this);
	RegFunct2(SetRealStarAutoUpdate, API::SetRealStarAutoUpdate, this);

	RegSFunct2(GetAddons, API::GetAddons, this);


	Class* cl = (Class *)FindType("PacketOutputAdapter");
	if(cl)
	{
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteBool, this);
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteInt, this);
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteFloat, this);
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteString, this);
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteVector, this);
		RegMethod(PacketOutputAdapter, PacketOutputAdapter, WriteMatrixAsQuaternionVector, this);
		m_PacketOutputAdapter.Init(cl);
	}

	cl = (Class *)FindType("PacketInputAdapter");
	if(cl)
	{
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadBool, this);
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadInt, this);
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadFloat, this);
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadString, this);
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadVector, this);
		RegMethod(PacketInputAdapter, PacketInputAdapter, ReadMatrixAsQuaternionVector, this);
		m_PacketInputAdapter.Init(cl);
	}

	InitPhysicsAPI();
#endif
}

}

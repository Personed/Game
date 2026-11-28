//-----------------------------------------------------------------------------
// File: enf_engine.cpp
//
// Desc: Basic engine class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "engine/enf_engine.h"
#include "system/enf_profiler.h"
#include "script/enf_scriptapi.h"
//#include "script/enf_scriptprofile.h"
#include "script/enf_scriptapivars.h"
#include "render/enf_rendmain.h"
//#include "sound/enf_xsoundsystem.h"
#include "common/enf_parser.h"
//#include "enf_input.h"
//#include "widgets/enf_widgets.h"
//#include "enf_stringtable.h"
#include "system/enf_vfilesystem.h"
//#include "baseworld/enf_decals.h"
#include "resource/enf_particleeffect.h"
#include "materials/enf_materialsystem.h"
#include "resource/enf_resourcemanager.h"
//#include "enf_pipeline.h"
//#include "widgets/enf_fonts.h"

#include "tinyxml/tinyxml.h"
#include "animations/enf_animationsystem.h"
#include "common/enf_streams.h"
//#include "Network/enf_network.h"
#include "system/enf_threadmanager.h"
#include "common/enf_diag.h"
//#include "baseworld/enf_worldphysics.h"
#include "common/enf_watchman.h"
#include "common/enf_staticbuffer.h"


/*
#ifndef _DEBUG
	int errno;
#endif
*/

//#undef ENF_ENABLE_DIAG

namespace enf
{

EnforceScriptContext* g_pScript = NULL;
Engine* g_Engine = NULL;
bool g_bGrabVideo = false;

/*
class StackChecker
{
protected:
	size_t	size;

public:
	StackChecker()
	{
		size = (0x200000 / sizeof(uint)) - 0x10000;
		uint* stackPtr = ALLOCA(uint, size);
		FillMemory32(stackPtr, size, 0xfadedf1d0);
	}

	void Check()
	{
		uint* stackPtr = ALLOCA(uint, size);
		for(size_t i = 0; i < size; i++)
		{
			if(stackPtr[i] == 0xfadedf1d0)
			{
				return;
			}
		}
		enf_assert(!"Stack overwritten");
	}
	~StackChecker()
	{
		Check();
	}
};

StackChecker stackChecker;
*/
//-----------------------------------------------------------------------
uint EngineStats::m_iLinkCount = 0;
uint EngineStats::m_iBTraceCount = 0;
uint EngineStats::m_iLTraceCount = 0;
uint EngineStats::m_iEntityCount = 0;
uint EngineStats::m_iBatchCount = 0;
uint EngineStats::m_iBatchBytesIndices = 0;
uint EngineStats::m_iBatchBytesVertexes = 0;
uint EngineStats::m_iLightCount = 0;
uint EngineStats::m_iDynamicVert = 0;
uint EngineStats::m_iDynamicInd = 0;
uint EngineStats::m_iStaticVert = 0;
uint EngineStats::m_iStaticInd = 0;
uint EngineStats::m_iDecals = 0;

uint EngineStats::m_iParticleCount = 0;
uint EngineStats::m_iMeshItemCount = 0;
uint EngineStats::m_iModelContextCount = 0;
uint EngineStats::m_iSpriteCount = 0;
uint EngineStats::m_iMeshCount = 0;
uint EngineStats::m_iApplyMaterialCount = 0;
uint EngineStats::m_iMaterialCount = 0;
uint EngineStats::m_iActiveBodies = 0;
uint EngineStats::m_iPassiveBodies = 0;

//-----------------------------------------------------------------------
bool	MainConfig::m_bEnableScreenshots = false;
bool	MainConfig::m_bEnableSuperScreenshots = false;
bool	MainConfig::m_bEnableVideoGrabber = false;
bool	MainConfig::m_bEnableDebugRender = false;
bool	MainConfig::m_bEnableConsole = false;
bool	MainConfig::m_bShowConsole = false;
bool	MainConfig::m_bLogConsole = false;
bool  MainConfig::m_bShowCrash = true;
bool	MainConfig::m_bShowConfig = false;

int	MainConfig::m_iOptimize = 10;
bool	MainConfig::m_bList = false;
LogLevel MainConfig::m_LogLevel = LL_NORMAL;
uint	MainConfig::m_iMemMegabytes = 0;

TLog	MainConfig::m_ConsoleLog;
TLog	MainConfig::m_CrashLog;


//-----------------------------------------------------------------------
Engine* GetEngine()
{
	enf_assert(g_Engine);
	return g_Engine;
}

//-----------------------------------------------------------------------------
bool IsEngine()
{
	return g_Engine != NULL;
}

//-----------------------------------------------------------------------------
int Engine::UpdateFrame(bool inscene)
{
	TimeStamp timeStamp = Timer::GetTicks();

	UpdateTime(timeStamp);

#ifdef ENF_DONE
	if(m_pScript)
		m_pScript->Frame(NULL);
#endif

	if(m_pResourceManager)
		m_pResourceManager->Update(timeStamp, 20);

	if( RenderFrame(inscene) != EE_OK )
		CThread::Sleep(50);

#ifdef ENF_DONE
	if(m_pScript)
		m_pScript->ResetStats();
#endif
	return true;
}

//-----------------------------------------------------------------------------
void CreateDiags()
{
	ENF_DIAG_MENU(DM_STATS_SUBMENU, "Statistics", NULL);
	{
		ENF_DIAG_REGISTER_BOOL(DM_STATS_FPS, "lctrl+num1", "FPS", "Statistics");
		ENF_DIAG_REGISTER(DM_STATS_STATS, "lctrl+num2", "General", "Statistics", "disable,basic,profile,all");
		ENF_DIAG_REGISTER(DM_STATS_SLOWFRAMES, "", "Slowframes", "Statistics", "off,20ms,40ms,60ms,80ms,100ms,150ms");
		ENF_DIAG_REGISTER(DM_STATS_SLOWSCRIPT, "", "Slow script", "Statistics", "off,1ms,2ms,5ms,10ms");
		ENF_DIAG_REGISTER(DM_STATS_SCRIPTPROFILER, "", "Script profiler", "Statistics", "off,frame,continuous");
		ENF_DIAG_REGISTER_SWITCH(DM_STATS_STRESSMEM, "", "Test mem-stress", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_STATS_FLUSHMEM, "", "Flush memory", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_DUMP_MEMORY, NULL, "Dump memory", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_MEMORY_SNAPSHOT, NULL, "Create memory snapshot", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_MEMORY_COMPARE, NULL, "Compare snapshot", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_DUMP_RESOURCES, NULL, "Dump resources", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_DUMP_ACTIVE_BODIES, NULL, "Dump active bodies", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_DUMP_ACTIVE_SOUNDS, NULL, "Dump active sounds", "Statistics");
		ENF_DIAG_REGISTER_BOOL(DM_DUMP_OBJECTS, NULL, "Dump rendered objects", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_STATS_COUNTER, "lctrl+num3", "Counters", "Statistics");
		ENF_DIAG_REGISTER_SWITCH(DM_STATS_NET, "lctrl+num4", "Network", "Statistics");
		ENF_DIAG_REGISTER(DM_STATS_SLOWDOWN, "","Slow down", "Statistics", "off,1ms,5ms,10ms,20ms,50ms,100ms");

	}
	ENF_DIAG_REGISTER(DM_MEMORY_VALIDATION, NULL, "Memory validation", NULL, "none,on,off");

	ENF_DIAG_MENU(DM_REND_SUBMENU, "Render", NULL);
	{
		ENF_DIAG_REGISTER_SWITCH(DM_REND_RELOADSHADERS, "rshift+f4", "Reload shaders", "Render");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_OFFLINESHADERS, NULL, "Offline shaders", "Render");

		ENF_DIAG_REGISTER(DM_REND_WIREFRAME, "lctrl+num0", "Wireframe", "Render", "disable,wire,onlywire");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_COMPLEX, NULL, "Depth-complex", "Render");
		ENF_DIAG_REGISTER(DM_REND_SHOWSHADOWMAP, NULL, "Show shadowmap", "Render", "disable,shadow map,omni shadow 1,omni shadow 2,omni shadow 3,omni shadow 4,omni shadow 5,omni shadow 6");
		ENF_DIAG_REGISTER(DM_REND_SHOWSHADOWFRUSTUM, NULL, "Show shadowmap boxes", "Render", "disable,far cascade,near cascade");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_SHOWDEPTHMAP, NULL, "Show depthmap", "Render");
		ENF_DIAG_REGISTER (DM_REND_SHOWTERRSHADOWMAP, NULL, "Terrain shadow map", "Render", "none,terrain shadow,shadow volume high,shadow volume low,shadow volume dist,disable");
		ENF_DIAG_REGISTER_BOOL(DM_REND_NORENDER, NULL, "No render", "Render");
		ENF_DIAG_REGISTER(DM_REND_MIPMAPBIAS, NULL, "Mipmap bias", "Render", "0,1,2,3,4,5,6,7,8,9,10,11,12,-1,-0.75,0.5,-0.25");
		ENF_DIAG_REGISTER(DM_REND_MAXMIPMAP, NULL, "Max mipmap", "Render", "0,1,2,3,4,5,6,7,8,9,10,11,12");
		ENF_DIAG_REGISTER(DM_REND_FORCE_LOD, NULL, "Force min LOD", "Render", "0,1,2,3,4,5,6,7,8,9");
		ENF_DIAG_REGISTER(DM_REND_LOD_MUL, NULL, "LOD multiplier", "Render", "1,0.25,0.5,0.75,1,1.5,2,2.5,3,3.5,4");
		ENF_DIAG_REGISTER_BOOL(DM_REND_DIRECT_DEPTH, NULL, "Direct depth resolve", "Render");
		ENF_DIAG_REGISTER_BOOL(DM_REND_PARTICLE_HISPEED, NULL, "Particle hi-precision simulation", "Render");
		ENF_DIAG_REGISTER_RSWITCH(DM_REND_DEPTHPASS, NULL, "Depth pass", "Render");

		ENF_DIAG_REGISTER_RSWITCH(DM_REND_CASCADESHADOW, NULL, "CascadeShadow", "Render");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_CASCADESHADOW_CULL, NULL, "CascadeShadow cull", "Render");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_MT, NULL, "Multithreaded", "Render");
		ENF_DIAG_REGISTER_RBOOL(DM_REND_OCCLUDERS, NULL, "Occluders", "Render");
		ENF_DIAG_REGISTER_RBOOL(DM_REND_OCCLUDERS_ENTS, NULL, "Occlude entities", "Render");
		ENF_DIAG_REGISTER_RBOOL(DM_REND_OCCLUDERS_VOL, NULL, "Occluders volumes", "Render");
		ENF_DIAG_REGISTER_BOOL(DM_REND_OCCLUDERS_ACTIVE, NULL, "Show active occluders", "Render");
		ENF_DIAG_REGISTER(DM_REND_CLEARRT, NULL, "Clear RT with ", "Render", "none,black,white,red,green,blue");

		ENF_DIAG_MENU(DM_REND_LIGHT_SUBMENU, "Lighting", "Render");
		{
			//#ifdef TEST_MRT
			ENF_DIAG_REGISTER(DM_REND_MRT, NULL, "Test MRT deffered lighting", "Lighting", "on,off,show albedo, show specular, show normal, show gloss, show lights");
			//#endif
			ENF_DIAG_REGISTER_RBOOL(DM_REND_LIGHTS, NULL, "Lights", "Lighting");
			ENF_DIAG_REGISTER_RBOOL(DM_REND_AMB, NULL, "Ambient lighting", "Lighting");
			ENF_DIAG_REGISTER_RBOOL(DM_REND_DIFF, NULL, "Diffuse lighting", "Lighting");
			ENF_DIAG_REGISTER_RBOOL(DM_REND_SPEC, NULL, "Specular lighting", "Lighting");
			ENF_DIAG_REGISTER(DM_REND_AMBBIAS, NULL, "Ambient bias", "Lighting", "0.0,0.25,0.5,0.75,1.0,1.25,1.5,1.75,2.0");
			ENF_DIAG_REGISTER(DM_REND_DIFFBIAS, NULL, "Diffuse bias", "Lighting", "0.0,0.25,0.5,0.75,1.0,1.25,1.5,1.75,2.0");
			ENF_DIAG_REGISTER(DM_REND_SPECBIAS, NULL, "Specular bias", "Lighting", "0.0,0.25,0.5,0.75,1.0,1.25,1.5,1.75,2.0");
		}

		ENF_DIAG_REGISTER_BOOL(DM_REND_SHADOWS, NULL, "Shadows", "Render");
		ENF_DIAG_REGISTER_RBOOL(DM_REND_REFLECTION, NULL, "Reflection", "Render");
		ENF_DIAG_REGISTER_SWITCH(DM_REND_SHOWREFL, NULL, "Show reflection", "Render");
		ENF_DIAG_REGISTER_RBOOL(DM_REND_DECALS, NULL, "Decals", "Render");

		ENF_DIAG_MENU(DM_REND_FILTER_SUBMENU, "Filter", "Render");
		{
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_MESH, NULL, "MeshObject", "Filter");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_MMODEL, NULL, "MModel", "Filter");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_SPRITE, NULL, "Sprite", "Filter");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_VOLLIGHT, NULL, "Volumetric lights", "Filter");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_PARTICLE, NULL, "Particle", "Filter");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_FILTER_ENTITIES, NULL, "All entities", "Filter");
		}

		ENF_DIAG_REGISTER_RSWITCH(DM_REND_POSTPROCESS, "lctrl+lalt+p", "Postprocess", "Render");
		ENF_DIAG_MENU(DM_REND_PP_SUBMENU, "PP effects", "Render");
		{
			ENF_DIAG_REGISTER(DM_REND_PP_SSAO, "lctrl+lalt+o", "SSAO", "PP effects", "on,off,force");
			ENF_DIAG_REGISTER_SWITCH(DM_REND_PP_SSAO_DEBUG, "lctrl+lalt+u", "SSAO debug", "PP effects");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_PP_DOF, NULL, "DOF", "PP effects");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_PP_FILMGRAIN, NULL, "Film grain", "PP effects");
			ENF_DIAG_REGISTER(DM_REND_PP_EDGEAA, NULL, "Edge AA", "PP effects", "on in SG,on as PP,debug,off");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_PP_SMARTGAMMA, NULL, "Smart gamma", "PP effects");
			ENF_DIAG_REGISTER(DM_REND_PP_SMARTGAMMA_MODE, NULL, "Smart gamma debug", "PP effects", "full,nobloom,original,bloom,aperture");
			ENF_DIAG_REGISTER(DM_REND_PP_APERTURE, NULL, "Show camera aperture", "PP effects", "none,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_PP_RAIN2, "lctrl+lalt+r", "Rain/Snow", "PP effects");
		}



		ENF_DIAG_REGISTER_RSWITCH(DM_TERR_TERRAIN, "lctrl+lalt+t", "Terrain", "Render");
		ENF_DIAG_REGISTER_RSWITCH(DM_TERR_GRASS, "lctrl+lalt+h", "Grass", "Render");
		ENF_DIAG_MENU(DM_TERR_MENU, "Terrain and grass", "Render");
		{
			ENF_DIAG_REGISTER_SWITCH(DM_TERR_LAYERS, "lctrl+lalt+l", "Layers diag mode", "Terrain and grass");
			ENF_DIAG_REGISTER(DM_TERR_LAYERS_MODE, NULL, "Layers mode", "Terrain and grass", "blend,soft alpha test");
			ENF_DIAG_REGISTER_SWITCH(DM_TERR_BBOX, "lctrl+lalt+b", "Blocks' bounding boxes", "Terrain and grass");
			ENF_DIAG_REGISTER_SWITCH(DM_TERR_BBOXTILE, "lctrl+lalt+v", "Tiles' bounding boxes", "Terrain and grass");
			ENF_DIAG_REGISTER_RSWITCH(DM_TERR_STEXTURE, "lctrl+lalt+s", "Detail texture", "Terrain and grass");

			ENF_DIAG_REGISTER(DM_TERR_RENDER_MODE, NULL, "Terrain render mode", "Terrain and grass", "by tiles,by blocks");
			ENF_DIAG_REGISTER(DM_TERR_GEOMDETAIL, NULL, "Geometric detail", "Terrain and grass", "normal, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10");
			ENF_DIAG_REGISTER(DM_TERR_TEXDETAIL, NULL, "Detail texture detail", "Terrain and grass", "normal, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10");
			ENF_DIAG_REGISTER(DM_TERR_GRASSDETAIL, "lctrl+lalt+z", "Grass distance", "Terrain and grass", "normal, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10");
			ENF_DIAG_REGISTER_SWITCH(DM_TERR_GRASSFREEZE, "lctrl+lalt+f", "Freeze grass pos", "Terrain and grass");
			ENF_DIAG_REGISTER_SWITCH(DM_TERR_GRASSBBOX, "lctrl+lalt+c", "Grass clusters' boxes", "Terrain and grass");		
			ENF_DIAG_REGISTER_BOOL(DM_TEST_GRASSMIP, NULL, "Show grass mipmaps", "Terrain and grass");
			ENF_DIAG_REGISTER(DM_TERR_GRASSLOD, NULL, "Force grass lod", "Terrain and grass", "default, 0, 1, 2, 3");
		}

		ENF_DIAG_MENU(DM_SKYOCEAN_MENU, "Ocean and sky", "Render");
		{
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY, NULL, "Sky", "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_FOG, NULL, "Fog", "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_LENS, "lctrl+lalt+l", "Lens Flares", "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_GODRAYS, "lctrl+lalt+g", "Godrays", "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_PLANETS, "", "Show planets",  "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_STARS, "", "Show stars",  "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_STARS_DEBUG, "", "Show real stars constellations",  "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_FARLAYER, "", "Show far layer",  "Ocean and sky");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_SKY_DYNLAYER, "", "Show dynamic layer",  "Ocean and sky");
			ENF_DIAG_REGISTER(DM_REND_OCEAN, NULL, "Ocean", "Ocean and sky", "enabled,disable simulation,disabled");
			ENF_DIAG_REGISTER_RSWITCH(DM_REND_OCEAN_LHEIGHT, NULL, "Ocean local height", "Ocean and sky");
			ENF_DIAG_REGISTER_SWITCH(DM_REND_OCEAN_FOAM, NULL, "Ocean cumulative foam", "Ocean and sky");
			ENF_DIAG_REGISTER(DM_REND_OCEAN_GRID, NULL, "Projection grid", "Ocean and sky", "normal, 128x256, 86x384,256x256, 512x256, 512x512, 768x512, 768x768, 1024x768, 1024x1024");
			ENF_DIAG_REGISTER(DM_REND_OCEAN_FFTRES, NULL, "FFT resolution", "Ocean and sky", "normal, 32, 64, 128, 256, 512, 1024, 2048");
			ENF_DIAG_REGISTER(DM_REND_OCEAN_SHOWTEX, NULL, "Show ocean tex", "Ocean and sky", "disable, height, height3, height6, height9, normal, derivate");
			ENF_DIAG_REGISTER(DM_REND_SHOWOCEANCUBE, NULL, "Show reflection map", "Ocean and sky", "none, side 1, side 2, side 3, side 4, side 5, side 6");
		}
	}

	#ifdef ENF_STEREO_VISION
		ENF_DIAG_REGISTER_SWITCH(DM_REND_STEREO_INFO, NULL, "Read stereo GPU info", "Render");	
		ENF_DIAG_REGISTER(DM_REND_STEREO_GUI_DEPTH, NULL, "Force stereo GUI depth", "Render", "none,1,2,3,4,5,6,7,8,9,10,far");
	#endif

	ENF_DIAG_MENU(DM_WORLD_SUBMENU, "World", NULL);
	{
		ENF_DIAG_REGISTER(DM_SHOW_AABB, "lctrl+num5", "Show AABB", "World", "disabled,mesh,particle,sprite");
		ENF_DIAG_REGISTER_RSWITCH(DM_SHOW_ACTIVE, NULL, "Show active", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_BODIES, "lctrl+num6", "Show bodies", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_BULLET, NULL, "Show Bullet", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_FORCE_SLEEP, NULL, "Force sleep Bullet", "World");

		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_OCCLUDERS, NULL, "Show occluders", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_BONES, NULL, "Show bones", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_WIND, "lctrl+num8", "Show wind emitters", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_DISABLE_WIND, "lctrl+num9", "Disable wind", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_DISABLE_PARTSORT, NULL, "Disable particle sort", "World");
		ENF_DIAG_REGISTER_RSWITCH(DM_PHYSICS, "lctrl+num7", "Physics", "World");
		ENF_DIAG_REGISTER(DM_PROFILE_BULLET, NULL, "Profile Bullet", "World", "disabled,>1ms,>5ms,>10ms,>20ms,>50ms");
		ENF_DIAG_REGISTER_RSWITCH(DM_PHYSICS_RAGDOLLS, NULL, "Reload ragdolls", "World");

		ENF_DIAG_REGISTER_RSWITCH(DM_GAMEUPDATE, NULL, "Game update", "World");	
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_CLIPS, "lctrl+m", "Show clips", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_SHOW_TREE, NULL, "Show scene tree", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_FORCE_UPDATE, NULL, "Force update", "World");
		ENF_DIAG_REGISTER_SWITCH(DM_FORCE_VISIBLE, NULL, "Force visible", "World");

		ENF_DIAG_REGISTER(DM_CAPTURE_CUBEMAP_SIZE, NULL, "Cube map size", "World", "32, 16, 32, 64, 128, 256, 512, 1024");
		ENF_DIAG_REGISTER_BOOL(DM_CAPTURE_CUBEMAP, NULL, "Capture cube map", "World");

		ENF_DIAG_REGISTER(DM_WORLD_FOV, NULL, "Force vertical FOV", "World", "default,65,70,75,80,85,90,95,100,105,110,120,2.5f,5,10,15,20,25,30,35,40,45,50,55");
		ENF_DIAG_REGISTER(DM_WORLD_FARPLANE, NULL, "Override far plane", "World", "default,2000,2500,3000,3500,4000,4500,5000,6000,7000,8000,9000,10000,11000,12000,13000,14000,15000");
	}

	ENF_PROFILER_SETSLOT(PROFILE_UPDATEENTITIES, "UpdateAllEntities", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_UPDATEENTITY_TICK, " EOnTick", 0);
	ENF_PROFILER_SETSLOT(PROFILE_UPDATEENTITY_BEFORE, " BeforeSimulate", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_SLOWENTITY, " Slow entity", 0);

	ENF_PROFILER_SETSLOT(PROFILE_PHYSICS_ADVANCE, " AdvancePhysics", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_PHYSICS_MOVE, "  PhysicsMove", PF_EVENT);

	ENF_PROFILER_SETSLOT(PROFILE_UPDATEENTITY_AFTER, " AfterSimulate", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_ENTITY_UPDATE, "  UpdateEntity", 0);
	ENF_PROFILER_SETSLOT(PROFILE_API_CALL, "   Entity events", 0);

	ENF_DIAG_REGISTER_SWITCH(DM_WATCHMAN_ENABLE, NULL, "Watchman", NULL);
	ENF_DIAG_REGISTER_SWITCH(DM_WATCHMAN_BREAK, NULL, "Watchman break", NULL);

	ENF_PROFILER_SETSLOT(PROFILE_WORLD_RENDER, "RenderWorld", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_SCENE_TRAVERSE, " SceneTraverse", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_ENTITY_RENDER, " RenderEntities", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_DECAL_RENDER, "  RenderDecals", 0);
	ENF_PROFILER_SETSLOT(PROFILE_SKY_RENDER, " RenderSky", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_OCEAN_RENDER, " RenderOcean", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_BUILD_MESH, " BuildMesh", 0);
	ENF_PROFILER_SETSLOT(PROFILE_PARTICLE_RENDER, " RenderParticles", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_TERRAIN_RENDER, " TerrainRender", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_TERRAIN_PREPARE, " TerrainPrepare", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_PIPELINE_RENDER, " PipelineRender", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_RENDERSHADOWS, "  RenderShadows", 0);
	ENF_PROFILER_SETSLOT(PROFILE_RENDERLIGHTS, "  RenderLights", 0);
	ENF_PROFILER_SETSLOT(PROFILE_RENDERDEPTHPASS, "  RenderDepthPass", 0);

	ENF_PROFILER_SETSLOT(PROFILE_BUILDSHADOWVOLUME, "   BuildShadowVolume", 0);
	ENF_PROFILER_SETSLOT(PROFILE_RENDERSHADOWVOLUME, "   RenderShadowVolume", 0);
	ENF_PROFILER_SETSLOT(PROFILE_RENDEROCCLUSIONQUERY, "  RenderOcclusions", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_ONRENDER_IDLE, "  OnRenderIdle", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_SCRIPTEXECUTE, "ExecuteScript", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_PROCESSTHREADS, "ProcessThreads", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_SOUNDMANAGER_UPDATE, "UpdateSounds", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_PATHFINDER_FINDPATH, "FindShortestPath", 0);

	ENF_PROFILER_SETSLOT(PROFILE_UPDATEPARTICLES, "UpdateParticles", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_OCEAN_CPUFFT, "Ocean CPU FFT", NULL);
	ENF_PROFILER_SETSLOT(PROFILE_MESHOBJECT_ANIM, "MeshAnim", 0);
	ENF_PROFILER_SETSLOT(PROFILE_MESHOBJECT_GETSKINNEDMESH, "GetSkinnedMesh", 0);
	ENF_PROFILER_SETSLOT(PROFILE_MESHOBJECT_MORPH, "MeshObjectMorph", 0);
	ENF_PROFILER_SETSLOT(PROFILE_WORLD_P2PVISIBILITY, "P2PVisibility", 0);
	ENF_PROFILER_SETSLOT(PROFILE_WORLD_TRACELINE, "TraceLine", 0);
	ENF_PROFILER_SETSLOT(PROFILE_WORLD_TRACEMOVE, "TraceMove", 0);
	ENF_PROFILER_SETSLOT(PROFILE_WORLD_TRACEPOSITION, "TracePosition", 0);
	ENF_PROFILER_SETSLOT(PROFILE_USER_0, "PrepareTerrain", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_PRESENT, "Present (GPU)", PF_EVENT);
	ENF_PROFILER_SETSLOT(PROFILE_LOCK_VB, "Lock VB", 0);
	ENF_PROFILER_SETSLOT(PROFILE_LOCK_IB, "Lock IB", 0);
	ENF_PROFILER_SETSLOT(PROFILE_NETWORK, "Network", PF_EVENT);

	ENF_PROFILER_SETSLOT(PROFILE_THREAD_ENQUEUE, "Thread enqueue", 0);
	ENF_PROFILER_SETSLOT(PROFILE_THREAD_WAITFOR, "Thread WaitFor", 0);
	ENF_PROFILER_SETSLOT(PROFILE_THREAD_WAITFORIDLE, "Thread WaitForIdle", 0);

	ENF_PROFILER_SETCOUNTER(0, "cull ent-list");
	ENF_PROFILER_SETCOUNTER(1, "cull ents");
}

//-----------------------------------------------------------------------------
// Name: Engine()
// Desc: Constructor
//-----------------------------------------------------------------------------
Engine::Engine(CStr appname, CStr cliparams, size_t staticPoolSize):
	m_pOutputStream(NULL),
	m_pErrorStream(NULL),
	m_pErrorHandler(NULL),
	m_pConsole(NULL),
	m_pWidgetManager(NULL),
	m_pInput(NULL),
	m_pFileSystem(NULL),
	m_pStatic(NULL),
	m_pScript(NULL),
	m_pRender(NULL),
	m_pParticleSystem(NULL),
	m_pResourceManager(NULL),
	m_pPipeline(NULL),
	m_pMaterialSystem(NULL),
	m_pDecalManager(NULL),
	m_pPathEngine(NULL),
	m_pAnimationSystem(NULL),
	m_pEntityManager(NULL),
	m_pWorldManager(NULL),
	m_pPhysicsManager(NULL),
	m_pXSoundSystem(NULL),
	m_pFont(NULL),
	m_iLastTime(0),
	m_iSettingsChanged(0),
	m_dTime(0.0),
	m_dTickDelta(0.0),
	m_dFixedTimeSlice(-1.0),
	m_dTimeScale(1.0),
	m_iLastDiagTime(0),
	m_iDiagActiveMenu(-1),
	m_bDiagShowMenu(false),
	m_bAppQuit(false),
	m_iLoading(0),
	m_bAlwaysUpdate(false),
	m_bPaused(false),
	m_bRuntimeMode(false)
{
	g_Engine = this;
#ifdef ENF_TIME_INTERPOLATE
	m_iBottomTime = 0;
	m_iTopTime = 0;
#endif

	IL_EXCHANGE(m_iFrameCount, 0);
	Timer::Init();

	AddEventHandler(this);

	if(appname.IsNull())
		m_strAppName = "Enfusion v" ENF_VERSION_STRING;
	else
		m_strAppName = appname.cstr();

	//init singletons etc.
	#ifdef ENF_DONE
		__mathInit();
	#endif

	ParseParams(cliparams.cstr());
	CreateDiags();

	//create it always, even empty
	m_pSettings = ENF_NEW TiXmlDocument();
	m_pSettings->InsertEndChild(TiXmlElement("config"));
	m_pSettings->RootElement()->SetAttribute("version", "1.0");

	enf_assert(g_pStatic == NULL);
	g_pStatic = m_pStatic = CreateStaticBuffer(staticPoolSize, true);

	if(m_pStatic == NULL)
	{
		enf_assert(!"Can't allocate static buffer");
	}

	TiXmlDocument::SetStaticBuffer(g_pStatic);

	InitMainThread();

	ZeroMem(m_pThreadManagers, sizeof(m_pThreadManagers));
	m_pThreadManagers[0] = ENF_NEW CThreadManager(256, 96 * 1024);
	m_pThreadManagers[0]->Start(1, "JobManager1");
	m_pThreadManagers[1] = ENF_NEW CThreadManager(256, 96 * 1024);
	m_pThreadManagers[1]->Start(2, "JobManager2");

	SetCurrent();
}

//-----------------------------------------------------------------------------
CStr Engine::GetVersion() const
{
	return ENF_VERSION_STRING;
}

//-----------------------------------------------------------------------------
uint Engine::GetVersionID() const
{
	return ENF_VERSION_ID;
}

//-----------------------------------------------------------------------------
int Engine::HandleEvent(const Event& event)
{
	if(event.HasType(SignStatusEventTypeID))
	{
		const SignStatusEvent& sevent = (const SignStatusEvent&)event;
		m_strProfileName = sevent.m_strUserName;
	}
	return 0;
}

//-----------------------------------------------------------------------------
bool Engine::IsSendingEvent(uint typeID) const
{
	return m_SentEvents.GetIndexOf(typeID) != -1;
}

//-----------------------------------------------------------------------------
void Engine::SendEvent(const Event& event)
{
	m_SentEvents.Insert(event.GetTypeID());

	for(uint n = 0; n < m_EventHandlers.GetCardinality(); n++)
	{
		m_EventHandlers[n]->HandleEvent(event);
	}
	
	for(uint n = 0; n < m_Modules.GetCardinality(); n++)
	{
		m_Modules[n]->OnEvent(event);
	}
	m_SentEvents.Pop();
}

//-----------------------------------------------------------------------------
void Engine::BeforeReset()
{
	SendEvent(BeforeResetEvent());
}

//-----------------------------------------------------------------------------
void Engine::AfterReset()
{
	SendEvent(AfterResetEvent());
}

//-----------------------------------------------------------------------------
void Engine::Destroy()
{
#ifndef ENF_CONSOLE
#ifdef WIN32
	::ShowCursor(true);
#endif

	if(m_iSettingsChanged)
		SaveSettings();
#endif

	m_pConsole = NULL;
	m_pWidgetManager = NULL;
	m_pDecalManager = NULL;
	m_pResourceManager = NULL;
	m_pMaterialSystem = NULL;
	m_pParticleSystem = NULL;
	m_pRender = NULL;
	m_pInput = NULL;
	m_pPathEngine = NULL;
	m_pFileSystem = NULL;

	//destroy modules!
	while(!m_Modules.IsEmpty())
	{
		Module* mod = m_Modules.RemoveElement(m_Modules.GetCardinality() - 1);
		mod->Release();
	}

	//destroy event handlers
	//Do not delete them! they may be static allocated!
	m_EventHandlers.Clear();

	SAFE_DELETE(m_pScript);
	g_pScript = NULL;

	SAFE_DELETE(m_pStatic);
	g_pStatic = NULL;

	SAFE_DELETE(m_pSettings);
	CParser::Cleanup();

	delete m_pThreadManagers[0];
	delete m_pThreadManagers[1];
	delete m_pThreadManagers[2];

//	stackChecker.Check();
}

//-----------------------------------------------------------------------------
Module* Engine::FindModule(uint type)
{
	for(uint n = 0; n < m_Modules.GetCardinality(); n++)
	{
		if(m_Modules[n]->HasType(type))
		{
			return m_Modules[n];
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
TiXmlElement* Engine::GetSettingsElement(const char* name) const
{
	return m_pSettings->RootElement()->FindOrCreateChildElement(name);
}

//-----------------------------------------------------------------------------
void Engine::ChangeSettings()
{
	m_iSettingsChanged++;
	TiXmlElement* elem = GetSettings()->RootElement();

	TiXmlElement* videoElem = elem->FindOrCreateChildElement("video");

	if(GetRender() && videoElem)
	{
		if(m_bRuntimeMode)
		{
			RenderSetup setup = GetRender()->GetSetup();
			bool doReset = false;

			CStr vsync = videoElem->Attribute("vsync");

			uint presentationInterval = (vsync == "0") ? 0 : 1;

			if(presentationInterval != setup.m_PresentationInterval)
			{
				setup.m_PresentationInterval = presentationInterval;
				doReset = true;
			}	

			CStr fs = videoElem->Attribute("fullscreen");
			if((setup.m_bWindowed && fs == "1") || (!setup.m_bWindowed && fs == "0"))
			{
				setup.m_bWindowed = !setup.m_bWindowed;
				doReset = true;
			}
			
			CStr width = videoElem->Attribute("screenwidth");
			CStr height = videoElem->Attribute("screenheight");
			if(setup.m_iBackBufferWidth != width.ToInt() || setup.m_iBackBufferHeight != height.ToInt())
			{
				setup.m_iBackBufferWidth = width.ToInt();
				setup.m_iBackBufferHeight = height.ToInt();
				doReset = true;
			}
			CStr cgamma = videoElem->FindOrCreateAttribute("gamma", "50");
			int gamma = enf_limit((cgamma.ToInt() / 2) + 50, 0, 100);
			GetRender()->SetBrightness(gamma);

			if(doReset)
			{
				Resize(setup.m_iBackBufferWidth, setup.m_iBackBufferHeight, 0, setup.m_bWindowed, presentationInterval == 1);
			}
		}
	}

	for(uint n = 0; n < m_Modules.GetCardinality(); n++)
	{
		m_Modules[n]->OnChangeSettings(elem);
	}
}

//-----------------------------------------------------------------------------
ebool Engine::AddEventHandler(EventHandler* eventhandler)
{
	return m_EventHandlers.Insert(eventhandler);
}

//-----------------------------------------------------------------------------
ebool Engine::RemoveEventHandler(EventHandler* eventhandler)
{
	return m_EventHandlers.Remove(eventhandler);
}

//-----------------------------------------------------------------------------
bool Engine::InsertModule(Module* mod)
{
	enf_assert(mod != NULL);

	if(m_Modules.GetIndexOf(mod) != -1)
		return false;

	enf_assert(mod->GetTypeID() != ModuleTypeID);

	mod->m_pEngine = this;

	if(g_Engine == this)
	{
		//Engine is current. Make the module current too!
		mod->SetCurrent();
	}

	//setup most common modules as direct pointers
	if(mod->HasType(InputDeviceTypeID))
		m_pInput = (InputDevice*)mod;
	else if(mod->HasType(RendererTypeID))
		m_pRender = (Renderer*)mod;
	else if(mod->HasType(ParticleSystemTypeID))
		m_pParticleSystem = (ParticleSystem*)mod;
	else if(mod->HasType(ResourceManagerTypeID))
		m_pResourceManager = (ResourceManager*)mod;
	else if(mod->HasType(MaterialSystemTypeID))
		m_pMaterialSystem = (MaterialSystem*)mod;
	else if(mod->HasType(FileSystemTypeID))
		m_pFileSystem = (FileSystem*)mod;
	else if(mod->HasType(DecalManagerTypeID))
		m_pDecalManager = (DecalManager*)mod;
	else if(mod->HasType(AnimationSystemTypeID))
		m_pAnimationSystem = (AnimationSystem*)mod;
	else if(mod->HasType(EntityManagerTypeID))
		m_pEntityManager = (EntityManager*)mod;
	else if(mod->HasType(WorldManagerTypeID))
		m_pWorldManager = (WorldManager*)mod;
	else if(mod->HasType(PhysicsManagerTypeID))
		m_pPhysicsManager = (PhysicsManager*)mod;
#ifdef ENF_DONE
	else if(mod->HasType(XSoundSystemTypeID))
		m_pXSoundSystem = (XSoundSystem*)mod;
	else if(mod->HasType(WidgetManagerTypeID))
	{
		m_pWidgetManager = (WidgetManager*)mod;

		m_pWidgetManager->UpdateWidgets(GetRender()->GetSetupWidth(), GetRender()->GetSetupHeight());
		m_pFont = m_pWidgetManager->LoadDefaultFont("system");

		//don't waste XBOX/PS3 memory
		#ifndef ENF_CONSOLE
			if((m_pConsole = (ConsoleWidget*)m_pWidgetManager->CreateWidget(ConsoleWidgetTypeID, NULL, Rect2D(0,0,m_pWidgetManager->GetWidth(),m_pWidgetManager->GetHeight() / 2), WF_BLEND|WF_SOURCEALPHA|WF_EXACTSIZE|WF_EXACTPOS|WF_IGNOREPOINTER, 0xbf7f7f7f, 0x7fffffff)) != NULL)
			{
				m_pConsole->LoadImageFile(0, "console01");
			}
		#endif
		LogF(LL_NORMAL, "Enforce engine ver.: "  ENF_VERSION_STRING);
	}
	else if(mod->HasType(PathEngineTypeID))
		m_pPathEngine = (PathEngine*)mod;
#endif
	else if(mod->HasType(PipelineTypeID))
		m_pPipeline = (Pipeline*)mod;

	m_Modules.Insert(mod);
	mod->OnInsert(m_pSettings->RootElement());

	for(uint n = 0; n < m_Modules.GetCardinality(); n++)
	{
		if(m_Modules[n] != mod)
		{
			enf_assert2(!m_Modules[n]->HasType(mod->GetTypeID()) && !mod->HasType(m_Modules[n]->GetTypeID()),
							"Cannot use two modules of the same ancestor");
			m_Modules[n]->OnModuleInserted(mod);
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
bool Engine::RemoveModule(Module* mod)
{
	enf_assert(mod != NULL);

	if(m_Modules.Remove(mod))
	{
		if(mod->HasType(WidgetManagerTypeID))
		{
			m_pWidgetManager = NULL;
			m_pFont = NULL;
			m_pConsole = NULL;
		}
		for(uint n = 0; n < m_Modules.GetCardinality(); n++)
		{
			m_Modules[n]->OnModuleRemoved(mod);
		}
		mod->m_pEngine = NULL;
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
void Engine::SetCurrent()
{
	g_Engine = this;

	for(uint n = 0; n < m_Modules.GetCardinality(); n++)
	{
		m_Modules[n]->SetCurrent();
	}

	g_pStatic = m_pStatic;
	g_pScript = m_pScript;
}

//-----------------------------------------------------------------------------
// Name: UpdateTime()
// Desc:
//-----------------------------------------------------------------------------
void Engine::UpdateTime(uint64 ctime)
{
	TimeStamp ticksdelta = Timer::Sub(ctime, m_iLastTime);
	m_iLastTime = ctime;

#ifdef ENF_TIME_INTERPOLATE
	m_iTimes[m_iTopTime] = ticksdelta;

	m_iTopTime = (m_iTopTime + 1) & (MAX_TIMES - 1);
	if(m_iTopTime == m_iBottomTime)
		m_iBottomTime = (m_iBottomTime + 1) & (MAX_TIMES - 1);
		
	//count times and make average time
	int64 total = 0;
	int tim = 0;

	for(int t = m_iBottomTime; t != m_iTopTime; t = (t + 1) & (MAX_TIMES - 1))
	{
		total += m_iTimes[t];
		tim++;
	}

	total /= tim;

	double dtotal = Timer::Tick2Time(total, Timer::RESOLUTION_MILLISECONDS);



#else
	double dtotal = Timer::Tick2Time(ticksdelta, Timer::RESOLUTION_MILLISECONDS);
#endif

	//limit 5..2000fps
	dtotal = enf_limit(dtotal, 0.5, 200.0);

	if(m_dFixedTimeSlice != -1.0f)
	{
		dtotal = m_dFixedTimeSlice;
	}

	dtotal *= m_dTimeScale;
	m_dTickDelta = dtotal;
	m_dTime += m_dTickDelta;
}

//-----------------------------------------------------------------------------
double Engine::SetTimeScale(double timescale)
{
	double prev = m_dTimeScale;

	m_dTimeScale = enf_limit(timescale, 0.01, 10.0);
	return prev;
}

//-----------------------------------------------------------------------------
double Engine::SetFixedTimeSlice(double timeslice)
{
	double prev = m_dFixedTimeSlice;
	m_dFixedTimeSlice = timeslice;
	return prev;
}

//-----------------------------------------------------------------------------
void Engine::ClearTime()
{
	m_iLastTime = Timer::GetTicks();
	m_dTime = 0.0f;
	m_dTickDelta = 0;
#ifdef ENF_TIME_INTERPOLATE
	m_iBottomTime = 0;
	m_iTopTime = 0;
#endif

}

//-----------------------------------------------------------------------------
void Engine::ConsoleMessage(const char* str)
{
char* tmp = (char*)str;
int len;
	if(tmp == NULL)
		return;
	len = strlen(tmp);
	if(len == 0)
		return;

	if(tmp[len - 1] != '\n')
	{
		tmp[len++] = '\n';
		tmp[len] = 0;
	}

	CMutexLock lock(m_Mutex);

	if(MainConfig::m_bLogConsole)
	{
		MainConfig::m_ConsoleLog << tmp;
	}

	if(m_pOutputStream)
		m_pOutputStream->Write((const unsigned char*)tmp, len + 1);

#ifdef ENF_DONE
#ifndef ENF_CONSOLE
	if(m_pConsole != NULL)
	{
		if(tmp[len - 1] == '\n')
			tmp[--len] = 0;
		tmp[255] = 0;

		m_pConsole->AddString(tmp);
	}
#endif
#endif
}
//-----------------------------------------------------------------------------
TLog ErrorLog("error.log", LOGF_CREATE);

//-----------------------------------------------------------------------------
void Engine::ErrorMessage(const char* str)
{
char* tmp = (char*)str;
int len;
	if(tmp == NULL)
		return;
	len = strlen(tmp);
	if(len == 0)
		return;

	if(tmp[len - 1] != '\n')
	{
		tmp[len++] = '\n';
		tmp[len] = 0;
	}

	CMutexLock lock(m_Mutex);

	ErrorLog << tmp;

	if(MainConfig::m_bEnableConsole)
		ConsoleMessage(tmp);
	
	return;
}

//-----------------------------------------------------------------------------
// Name: 
// Desc:
//-----------------------------------------------------------------------------
uint g_iFPS;
double g_fFPS;

//-----------------------------------------------------------------------
int Engine::FPS(uint64 ctime)
{
static TimeStamp ltime = 0;
static int tn = 0;

	if(tn == 10)
	{
		if(ctime > ltime)
		{
			double t = 10.0 / Timer::Tick2Time(Timer::Sub(ctime, ltime), 1);
			g_iFPS = (uint)t;
			g_fFPS = t;
		}
		else
		{
			g_iFPS = 999;
			g_fFPS = 999;
		}
		ltime = ctime;
		tn = 0;
	}

	tn++;

	return g_iFPS;
}

//-----------------------------------------------------------------------------
void GetValueName(const char* values, int value, char* buff)
{
	*buff = 0;
	const char* beg = values;
	int p = 0;
	while(*values)
	{
		if(*values == ',')
		{
			if(value == p)
			{
				memcpy(buff, beg, values - beg);
				buff[values - beg] = 0;
				return;
			}
			p++;
			beg = values + 1;
			values = beg;
			continue;
		}
		values++;
	}
	memcpy(buff, beg, values - beg);
	buff[values - beg] = 0;
	/*
	int minus = 1;
	if(*values == '-')
	{
		minus = -1;
		values++;
	}

	if(*values >= '0' && *values <= '9')
	{
		minv = atol(values) * minus;
		values = skipnumbers(values);
	}*/
}

//-----------------------------------------------------------------------------
void DiagValue2String(const char* name, const char* valuenames, int value, SString<1024>& buff)
{
	buff = name;
	if(valuenames)
	{
		buff += ": ";
		GetValueName(valuenames, value, buff.Last());
	}
}

//-----------------------------------------------------------------------------
void Engine::ProcessDiagControl()
{
#ifdef ENF_ENABLE_DIAG
	for(uint n = 0; n < Diag::MAX_DIAG_VALUES; n++)
	{
		if(Diag::Modes[n].Name == NULL || Diag::Modes[n].KeyCode == 0) continue;

		if(!(_GetAsyncKeyState(Diag::Modes[n].KeyCode) & 0x8000))
		{
			Diag::Modes[n].Pressed = false;
			continue;
		}
		uint qualifier = Diag::Modes[n].KeyQualifier;
		if(qualifier != 0)
		{
			int qualifiers[] = {SysKey::LCTRL, SysKey::RCTRL, SysKey::LSHIFT, SysKey::RSHIFT, SysKey::MENU, SysKey::MENU};

			bool qualified = true;
			for(uint q = 0; q < 6; q++)
			{
				if(qualifier & (1 << q))
				{
					if(!(_GetAsyncKeyState(qualifiers[q]) & 0x8000))
					{
						qualified = false;
						break;
					}
				}
			}

			if(!qualified)
			{
				Diag::Modes[n].Pressed = false;
				continue;
			}
		}

		if(Diag::Modes[n].Pressed)
			continue;

		Diag::Modes[n].Pressed = true;
		Diag::Values[n]++;
		if(Diag::Values[n] > Diag::Modes[n].Max)
			Diag::Values[n] = Diag::Modes[n].Min;

		DiagValue2String(Diag::Modes[n].Name, Diag::Modes[n].ValueNames, Diag::Values[n], m_strDiagBuff);
		m_iLastDiagTime = Timer::GetTicks();
	}

	bool kdown = false;
	bool kup = false;
	bool kleft = false;
	bool kright = false;
	bool kback = false;
	m_bDiagShowMenu = false;

	InputDevice* idev = GetInputDevice();
	ubyte buttons[16];
	int povs[2];
	uint joystick = idev->GetPreferredJoystick();
	if(joystick != 0)
	{
		idev->GetJoyButtons(joystick, buttons, 16);

		if(buttons[X360_GAMEPAD_LEFT_SHOULDER] && buttons[X360_GAMEPAD_RIGHT_SHOULDER])
			m_bDiagShowMenu = true;

		if(m_bDiagShowMenu)
		{
			idev->GetJoyPOVs(joystick, povs, 2);
			if(povs[0] == 0)
				kup = true;
			else if(povs[0] == 9000)
				kright = true;
			else if(povs[0] == 18000)
				kdown = true;
			else if(povs[0] == 27000)
				kleft = true;
			kback = (buttons[X360_GAMEPAD_BACK] != 0);
		}
	}

	if(!m_bDiagShowMenu)
	{
		m_bDiagShowMenu = (_GetAsyncKeyState(SysKey::LSUPER) & 0x8000) != 0 || (_GetAsyncKeyState(SysKey::RSUPER) & 0x8000) != 0;
		if(m_bDiagShowMenu)
		{
			kdown = ((_GetAsyncKeyState(SysKey::DOWN) & 0x8000) != 0);
			kup = ((_GetAsyncKeyState(SysKey::UP) & 0x8000) != 0);
			kleft = ((_GetAsyncKeyState(SysKey::LEFT) & 0x8000) != 0);
			kright = ((_GetAsyncKeyState(SysKey::RIGHT) & 0x8000) != 0);
			kback = ((_GetAsyncKeyState(SysKey::BACKSPACE) & 0x8000) != 0);
		}
	}

	if(m_bDiagShowMenu)
		idev->Reset(IMASK_KEYBOARD|IMASK_JOYSTICK);
	else
		return;

	if(m_iDiagActiveMenu < 0)
	{
		for(uint n = 0; n < Diag::MAX_DIAG_VALUES; n++)
		{
			if(Diag::Modes[n].Name && Diag::Modes[n].Parent == -1)
			{
				m_iDiagActiveMenu = n;
				break;
			}
		}
	}

	if(m_iDiagActiveMenu < 0)
		return;

	static bool pressed = false;
	static TimeStamp lastPressed = 0;
	static TimeStamp autorepeatTime = 0;

	int parent = Diag::Modes[m_iDiagActiveMenu].Parent;

	if(!kdown && !kup && !kleft && !kright && !kback)
	{
		pressed = false;
		autorepeatTime = 0;
	}

	bool doPress = false;
	TimeStamp ctime = Timer::GetTicks();
	if(pressed)
	{
		if(autorepeatTime == 0)
			autorepeatTime = ctime;


		if(Timer::Tick2Time(Timer::Sub(ctime, autorepeatTime), Timer::RESOLUTION_MILLISECONDS) > 200 &&
			Timer::Tick2Time(Timer::Sub(ctime, lastPressed), Timer::RESOLUTION_MILLISECONDS) > 23)
		{
			lastPressed = ctime;
			pressed = false;
			doPress = true;
		}
	}
	else if(!pressed && (kdown || kup || kleft || kright || kback))
	{
		doPress = true;
	}
	
	if(!pressed)
	{
		if(kdown)
		{
			for(int next = m_iDiagActiveMenu + 1; next < Diag::MAX_DIAG_VALUES; next++)
			{
				if(Diag::Modes[next].Name && Diag::Modes[next].Parent == parent)
				{
					m_iDiagActiveMenu = next;
					goto downDone;
				}
			}
			for(int next = 0; next < m_iDiagActiveMenu+1; next++)
			{
				if(Diag::Modes[next].Name && Diag::Modes[next].Parent == parent)
				{
					m_iDiagActiveMenu = next;
					break;
				}
			}
downDone:;
		}

		if(kup)
		{
			for(int next = m_iDiagActiveMenu-1; next >= 0; next--)
			{
				if(Diag::Modes[next].Name && Diag::Modes[next].Parent == parent)
				{
					m_iDiagActiveMenu = next;
					goto upDone;
				}
			}
			for(int next = Diag::MAX_DIAG_VALUES - 1; next > m_iDiagActiveMenu+1; next--)
			{
				if(Diag::Modes[next].Name && Diag::Modes[next].Parent == parent)
				{
					m_iDiagActiveMenu = next;
					break;
				}
			}
upDone:;
		}

		if(kback)
		{
			if(Diag::Modes[m_iDiagActiveMenu].Parent != -1)
			{
				m_iDiagActiveMenu = Diag::Modes[m_iDiagActiveMenu].Parent;
				parent = Diag::Modes[m_iDiagActiveMenu].Parent;
			}
		}
		if(kleft)
		{
			Diag::Values[m_iDiagActiveMenu]--;
			if(Diag::Values[m_iDiagActiveMenu] < Diag::Modes[m_iDiagActiveMenu].Min)
				Diag::Values[m_iDiagActiveMenu] = Diag::Modes[m_iDiagActiveMenu].Max;
		}
		if(kright)
		{
			if(Diag::Modes[m_iDiagActiveMenu].ValueNames == NULL)
			{
				for(int next = 0; next < Diag::MAX_DIAG_VALUES; next++)
				{
					if(Diag::Modes[next].Name && Diag::Modes[next].Parent == m_iDiagActiveMenu)
					{
						m_iDiagActiveMenu = next;
						break;
					}
				}
			}
			else
			{
				Diag::Values[m_iDiagActiveMenu]++;
				if(Diag::Values[m_iDiagActiveMenu] > Diag::Modes[m_iDiagActiveMenu].Max)
					Diag::Values[m_iDiagActiveMenu] = Diag::Modes[m_iDiagActiveMenu].Min;
			}
		}
	}
	if(doPress)
	{
		pressed = true;
		lastPressed = ctime;
	}
#endif
}

//-----------------------------------------------------------------------------
void Engine::ProcessDiagMenu(uint64 timeStamp)
{
#ifdef ENF_WATCHMAN
	if(ENF_DIAG_BOOL(DM_WATCHMAN_ENABLE))
	{
		ENF_WATCH_ENABLE();
	}
	else
	{
		ENF_WATCH_DISABLE();
	}

	if(ENF_DIAG_BOOL(DM_WATCHMAN_BREAK))
	{
		ENF_WATCH_BREAK();
		ENF_DIAG_SET(DM_WATCHMAN_BREAK, 0);
	}
#endif

	if(ENF_DIAG_VALUE(DM_MEMORY_VALIDATION))
	{
		if(ENF_DIAG_VALUE(DM_MEMORY_VALIDATION) == 1)
		{
			EnableMemoryValidation();
			ENF_DIAG_SET(DM_MEMORY_VALIDATION, 0);
		}
		else if(ENF_DIAG_VALUE(DM_MEMORY_VALIDATION) == 2)
		{
			DisableMemoryValidation();
			ENF_DIAG_SET(DM_MEMORY_VALIDATION, 0);
		}
	}

#ifdef ENF_LEAK_DETECTOR
	if(ENF_DIAG_BOOL(DM_DUMP_MEMORY))
	{
		MemoryManager::Dump("--------------------- Memory dump -----------------------------\n", MD_NODATA|MD_NOID);
		g_pSmallBlockAllocator->Dump("--------------- Small block memory dump -----------------------\n", MD_NODATA|MD_NOID|MD_NOSTACK);
		ENF_DIAG_SET(DM_DUMP_MEMORY, false);
	}

	if(ENF_DIAG_BOOL(DM_MEMORY_SNAPSHOT))
	{
		MemoryManager::CreateSnapshot();
		ENF_DIAG_SET(DM_MEMORY_SNAPSHOT, false);
	}

	if(ENF_DIAG_BOOL(DM_MEMORY_COMPARE))
	{
		MemoryManager::CompareSnapshot();
		ENF_DIAG_SET(DM_MEMORY_COMPARE, false);
	}


#endif
	if(ENF_DIAG_BOOL(DM_DUMP_RESOURCES))
	{
		LogF(LL_NORMAL, "===== Dump resources =====");
		GetRender()->DumpResources();
		ENF_DIAG_SET(DM_DUMP_RESOURCES, false);
	}

#ifdef EN_DONE
	if(ENF_DIAG_BOOL(DM_DUMP_OBJECTS))
	{
		LogF(LL_NORMAL, "Object statistics");
		GetPipeline()->PrintStats();
		ENF_DIAG_SET(DM_DUMP_OBJECTS, false);
	}
#endif

#ifdef ENF_ENABLE_DIAG
	if(!m_strDiagBuff.IsEmpty())
	{
		if(Timer::Tick2Time(Timer::Sub(timeStamp, m_iLastDiagTime), Timer::RESOLUTION_MILLISECONDS) > 1200)
		{
			m_strDiagBuff.Clear();
		}
		else
		{
			//m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

			if(m_pFont && m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
			{
				Color c;

				m_pFont->RenderText( 20, 20, m_strDiagBuff.cstr(), m_strDiagBuff.Length(), 0xffffffff, 0.0f);
				m_pFont->End();
			}
		}
	}

	if(m_bDiagShowMenu)
	{
		if(m_iDiagActiveMenu >= 0)
		{
			int parent = Diag::Modes[m_iDiagActiveMenu].Parent;

			m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

			if(m_pFont && m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
			{
				uint yspace = m_pFont->GetHeight();
				int ypos = 40;
				int xpos = 200;
				for(uint n = 0; n < Diag::MAX_DIAG_VALUES; n++)
				{
					if(Diag::Modes[n].Name == NULL || Diag::Modes[n].Parent != parent)
						continue;

					char mbuff[1024];

					const int MAX_LEN = 48;

					memset(mbuff, ' ', MAX_LEN);

					memcpy(mbuff, Diag::Modes[n].Name, enf::strlen(Diag::Modes[n].Name));
					if(Diag::Modes[n].ValueNames)
					{
						GetValueName(Diag::Modes[n].ValueNames, Diag::Values[n], mbuff + 24);
						int len = enf::strlen(mbuff);
						if(len < MAX_LEN)
							memset(mbuff + len, ' ', MAX_LEN - len);
						if(Diag::Modes[n].KeyControl)
						{
							int len = strlen(Diag::Modes[n].KeyControl);
							mbuff[35] = '(';
							memcpy(mbuff + 36, Diag::Modes[n].KeyControl, len);
							mbuff[36 + len] = ')';
						}
					}
					mbuff[MAX_LEN] = 0;

					m_pFont->RenderText( xpos, ypos, mbuff, enf::strlen(mbuff), n == m_iDiagActiveMenu ? 0xffffffff : (Diag::Modes[n].ValueNames ? 0xffa0a0a0 : 0xffa0a0ff), 0.0f);
					ypos += yspace;
				}
				m_pFont->End();
			}
		}
	}
#endif
}

//-----------------------------------------------------------------------------
void Engine::ProcessStatistics(uint64 timeStamp)
{

#ifdef ENF_DEVELOPER
	//avoid recursion
	if(Profiler::IsInside())
		return;
	if(m_pFont == NULL)
		return;

	Profiler::EndFrame();
	uint yoffset = 0;
	uint yspace = m_pFont->GetHeight();
	uint xoffset = 0;
	
	uint pageYOffset = 0;
	static uint NEW_PAGE_X_OFFSET = 500;

#if defined(ENF_CONSOLE)
	yoffset = 1;
	xoffset = 1;
#endif

	const float hscale = 1.0f;
	const float vscale = 1.0f;

	if(ENF_DIAG_VALUE(DM_STATS_SCRIPTPROFILER) > 0)
	{
		m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

		if(m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
		{
			g_pScript->SetProfiler(true);
			static ScriptProfile prof;
			static int count = 0;

			const int period = 20;
			if(count++ > period)
			{
				prof.Clear();

				if(ENF_DIAG_VALUE(DM_STATS_SCRIPTPROFILER) > 1)
					g_pScript->Profile(prof, SPF_RECURSIVE);
				else
					g_pScript->Profile(prof, SPF_RECURSIVE|SPF_RESET);

				prof.Sort();
			}
			SString<> tmp;
			tmp = "Time per class";
			m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffffff, 0, hscale, vscale);
			yoffset += yspace + 1;

			for(uint n = 0; n < prof.m_ClassByTime.GetCardinality() && n < 20; n++)
			{
				const Class* clss = prof.m_Classes[prof.m_ClassByTime[n].Data];
				tmp.format("%09.2f %s (%s)\0", (float)Timer::Tick2Time(prof.m_ClassByTime[n].Key, 100000), clss->GetName(), clss->GetModule()->GetName().cstr());
				m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffbfbfbf, 0, hscale, vscale);
				yoffset += yspace;
			}

			uint yoffset2 = 0;
			tmp = "Time per function";
			m_pFont->RenderText( 512, yoffset2, tmp.cstr(), tmp.Length(), 0xffffffff, 0, hscale, vscale);
			yoffset2 += yspace + 1;
			for(uint n = 0; n < prof.m_FunctionByTime.GetCardinality() && n < 20; n++)
			{
				const ScriptProfile::Item& item = prof.m_Functions[prof.m_FunctionByTime[n].Data];

				if(item.m_pClass)
					tmp.format("%09.2f %s::%s (%s)\0", (float)Timer::Tick2Time(prof.m_FunctionByTime[n].Key, 100000), item.m_pClass->GetName(), item.m_pFunct->GetName(), item.m_pFunct->GetModule()->GetName().cstr());
				else
					tmp.format("%09.2f %s (%s)\0", (float)Timer::Tick2Time(prof.m_FunctionByTime[n].Key, 100000), item.m_pFunct->GetName(), item.m_pFunct->GetModule()->GetName().cstr());
				m_pFont->RenderText( 512, yoffset2, tmp.cstr(), tmp.Length(), 0xffbfbfbf, 0, hscale, vscale);
				yoffset2 += yspace;
			}

			yoffset = enf_max(yoffset, yoffset2);

			yoffset = 384;
			tmp = "Count per function";
			m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffffff, 0, hscale, vscale);
			yoffset += yspace + 1;
			for(uint n = 0; n < prof.m_FunctionByCount.GetCardinality() && n < 20; n++)
			{
				const ScriptProfile::Item& item = prof.m_Functions[prof.m_FunctionByCount[n].Data];

				if(item.m_pClass)
					tmp.format("%08d %s::%s (%s)\0", prof.m_FunctionByCount[n].Key, item.m_pClass->GetName(), item.m_pFunct->GetName(), item.m_pFunct->GetModule()->GetName().cstr());
				else
					tmp.format("%08d %s (%s)\0", prof.m_FunctionByCount[n].Key, item.m_pFunct->GetName(), item.m_pFunct->GetModule()->GetName().cstr());
				m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffbfbfbf, 0, hscale, vscale);
				yoffset += yspace;
			}
			m_pFont->End();
		}
	}
	else
		g_pScript->SetProfiler(true);

	if(ENF_DIAG_BOOL(DM_STATS_NET))
	{
		Network* net = (Network*)FindModule(NetworkTypeID);

		if(net)
		{
			NetHost* host = (net->GetNumClients() != 0) ? (NetHost*)net->GetNetClient(0) : ((net->GetNumServers() != 0) ? (NetHost*)net->GetNetServer(0) : NULL);
			if(host)
			{
				m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

				if(m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
				{
					const NetStats& stats = host->GetStats();

					SString<> tmp;
					
					tmp.format("General:   Updates %04d | Dropped %03d | Invalid %03d\0",
						stats.m_iUpdates,
						stats.m_iPacketsDropped,
						stats.m_iInvalidMessages);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
					yoffset += yspace;

					tmp.format("RPC:       Sent %04d | Received %04d\0",
						stats.m_iRPCSent,
						stats.m_iRPCReceived);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
					yoffset += yspace;

					tmp.format("Reliable:   Sent packets: %04d | Sent bytes: %06d | Received packets %04d | Received bytes %06d\0",
						stats.m_iReliablePacketsSent,
						stats.m_iReliableBytesSent,
						stats.m_iReliablePacketsReceived,
						stats.m_iReliableBytesReceived);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
					yoffset += yspace;

					tmp.format("Unreliable: Sent packets: %04d | Sent bytes: %06d | Received packets %04d | Received bytes %06d\0",
						stats.m_iUnreliablePacketsSent,
						stats.m_iUnreliableBytesSent,
						stats.m_iUnreliablePacketsReceived,
						stats.m_iUnreliableBytesReceived);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
					yoffset += yspace;

					tmp.format("NetObjects: Created: %04d | Destroyed: %04d | Msg sent %05d | Msg received %05d | Skipped %05d\0",
						stats.m_iNetObjectsCreated,
						stats.m_iNetObjectsDestroyed,
						stats.m_iNetObjectsMessagesSent,
						stats.m_iNetObjectsMessagesReceived,
						stats.m_iNetObjectsSkipped);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
					yoffset += yspace;

					m_pFont->End();
				}
			}
		}
	}

	bool enableProfiler = false;

	if(ENF_DIAG_VALUE(DM_STATS_STATS) || ENF_DIAG_BOOL(DM_STATS_FPS))
	{
		m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

		if(m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
		{
			if(ENF_DIAG_VALUE(DM_STATS_STATS) & 1)
			{
				const RenderStats& rstats = m_pRender->GetStats();
				m_strFrameStats.format("Fps:%03d|DP:%04d Tri:%05d Vert:%06d|PS:%02d VS:%02d TX:%03d VB:%02d IB:%02d RS:%03d DSS:%03d BS:%03d SS:%03d",
					g_iFPS,
					rstats.NumPrimitives,
					rstats.NumTriangles,
					rstats.NumVertexes,
					rstats.PSChanges,
					rstats.VSChanges,
					rstats.TexChanges,
					rstats.VBChanges,
					rstats.IBChanges,
					rstats.RSChanges,
					rstats.DSSChanges,
					rstats.BSChanges,
					rstats.SSChanges
					);

				m_pFont->RenderText( xoffset,  yoffset, m_strFrameStats.cstr(), m_strFrameStats.Length(), 0xffffffff, 0, hscale, vscale);
				yoffset += yspace + 5;
				
				m_strFrameStats.format("MC:%04d MI:%04d|Part:%05d Mesh:%02d Spr:%02d Dec:%02d|MatChng:%03d\0",
					EngineStats::m_iModelContextCount,
					EngineStats::m_iMeshItemCount,
					EngineStats::m_iParticleCount,
					EngineStats::m_iMeshCount,
					EngineStats::m_iSpriteCount,
					EngineStats::m_iDecals,
					EngineStats::m_iApplyMaterialCount
					);

				m_pFont->RenderText( xoffset,  yoffset, m_strFrameStats.cstr(), m_strFrameStats.Length(), 0xffffffff, 0, hscale, vscale);
				yoffset += yspace + 5;

				SString<> tmp;
				tmp.format("WORLD: Ents %04d | Link: %04d | Trace: AABB %04d Line %04d | Lights: %02d\0",
					EngineStats::m_iEntityCount,
					EngineStats::m_iLinkCount,
					EngineStats::m_iBTraceCount,
					EngineStats::m_iLTraceCount,
					EngineStats::m_iLightCount);

				m_pFont->RenderText( xoffset, yoffset, tmp.cstr(), tmp.Length(), 0xffffff00, 0, hscale, vscale);
				yoffset += yspace;

				if(EngineStats::m_iDynamicVert || EngineStats::m_iDynamicInd || EngineStats::m_iStaticVert || EngineStats::m_iStaticInd)
				{
					tmp.format("Dynamic: Vertex %06dB | Index %06dB | Static: Vertex %06dB | Index %06dB",
						EngineStats::m_iDynamicVert, EngineStats::m_iDynamicInd, EngineStats::m_iStaticVert, EngineStats::m_iStaticInd);
					m_pFont->RenderText( 2, yoffset, tmp.cstr(), tmp.Length(), 0xffff40e0, 0, hscale, vscale);
					yoffset += yspace;
				}

				tmp.format("VM: Alloc %06d | StatAlloc %05dK/%05dK | Inst %05d | IPF %06d | Calls %05d | Threads %03d\0",
					m_pScript->GetStats()->m_iAllocatedMemory,
					m_pStatic->Allocated() >> 10,
					m_pStatic->GetSize() >> 10,
					m_pScript->GetStats()->m_iNumInstances,
					m_pScript->GetStats()->m_iExecuted,
					m_pScript->GetStats()->m_iCalls,
					m_pScript->GetStats()->m_iNumThreads);

				m_pFont->RenderText( xoffset, yoffset, tmp.cstr(), tmp.Length(), 0xffb0e0ff, 0, hscale, vscale);
				yoffset += yspace;

	#ifdef ENF_XENON
				MEMORYSTATUS stat;

			// Get the memory status.
				GlobalMemoryStatus( &stat );
				tmp.format("Used:%dM/%d%%.Anm.:%.2fM Tex.:%.2fM/%.2fM Ind.:%.2fM Vrt.:%.2fM\0",
					((stat.dwTotalPhys - stat.dwAvailPhys) / 1048576),
					(((stat.dwTotalPhys - stat.dwAvailPhys) / 1048576) * 100) / (stat.dwTotalPhys / 1048576),
					(float)MemStats::m_iAnims / 1048576.0f,
					(float)MemStats::m_iTextures / 1048576.0f,
					(float)MemStats::m_iDisposedTextures / 1048576.0f,
					(float)MemStats::m_iTotalIndices / 1048576.0f,
					(float)MemStats::m_iTotalVertices / 1048576.0f
					);
	#else
				tmp.format("VRAM: Total used %dM | TEX %d/%dK | RT %dK | IB %dK | VB %dK\0",
					m_pRender->GetAllocatedMemory() / 1048576,
					MemStats::m_iTextures >> 10,
					MemStats::m_iDisposedTextures >> 10,
					MemStats::m_iRenderTargets >> 10,
					MemStats::m_iTotalIndices >> 10,
					MemStats::m_iTotalVertices >> 10
					);
	#endif
				m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffffb0b0, 0, hscale, vscale);
				yoffset += yspace;

				//
				ResourceInfo info;
				GetResourceManager()->GetInfo(info);
				tmp.format("ResMan: Loaded %06dK | Loading %04d | Loaded %04d | Obj %04d | DObj %04d | Tex %04d\0",
					info.m_iLoadedBytes >> 10,
					info.m_iLoadingResources,
					info.m_iLoadedResource,
					info.m_iNumObjects,
					info.m_iNumDisposableObjects,
					info.m_iNumTextures
					);
				m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffff1f1f, 0, hscale, vscale);
				yoffset += yspace;


				tmp.format("Data %dK | Anm %dK | Obj %dK | Parts %dK\0",
					MemStats::m_iLevelData >> 10,
					MemStats::m_iAnims >> 10,
					MemStats::m_iObjects >> 10,
					MemStats::m_iParticles >> 10
					);
				m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffe0ff00, 0, hscale, vscale);
				yoffset += yspace;

				if(GetSoundSystem())
				{
					XSoundSystemStats stats;
					GetSoundSystem()->GetStats(stats);

					tmp.format("Sound: Samples %d/%d(%dK) | Memory %dK | Total voices %d | Ambients %d | Spatial %3d | Streams %d  | Stopped %3d\0",
						stats.NumLoadedSamples,
						stats.NumSamples,
						(stats.SamplesMem / 1024),
						(MemStats::m_iSoundSystem / 1024),
						stats.NumVoices,
						stats.NumAmbients,
						stats.NumPlaying3DSounds,
						stats.NumStreams,
						stats.NumStoped3DSounds);

					m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffff64ff, 0, hscale, vscale);
					yoffset += yspace;
				}

				if(GetEngine()->GetPhysicsManager())
				{
					tmp.format("Physics: AllocC %05d | Alloc %05dK. Active %05d Passive %05d\0",
						
						GetEngine()->GetPhysicsManager()->GetAllocatedBlocks(),
						(int)(GetEngine()->GetPhysicsManager()->GetAllocation() / 1024),
						EngineStats::m_iActiveBodies,
						EngineStats::m_iPassiveBodies
						);
					m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffffff64, 0, hscale, vscale);
					yoffset += yspace;
				}


				size_t totC = MemoryManager::GetTotalAllocations();
				size_t tot = MemoryManager::GetTotalAllocated();
				size_t over = MemoryManager::GetAllocOverhead();

				size_t physC = MemoryManager::GetTotalPhysicalAllocations();
				size_t phys = MemoryManager::GetTotalPhysicalAllocated();
			
				size_t loc = 0;

				tmp.format("Mem: TotC %05d | Tot %08dK | MemC %05d | Mem %08dK | PhysC %05d | Phys %08dK | Overhead %06dK",
					totC + physC,
					(int)((tot + phys + loc) >> 10),
					totC,
					(int)(tot >> 10),
					physC,
					(int)(phys >> 10),
					(int)(over >> 10)
				);

				m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xff4fafff, 0, hscale, vscale);
				yoffset += yspace;

				MemoryStats stats;
				g_pSmallBlockAllocator->GetStats(stats);
				
				tmp.format("SmallBlock: TotC %05d | Tot %08dK | Overhead %06dK | Blocks %05d\0",
					stats.NumAllocations,
					stats.Allocated >> 10,
					stats.Overhead >> 10,
					stats.NumBlocks);

				m_pFont->RenderText( xoffset, yoffset, tmp.cstr() , tmp.Length(), 0xffffff00, 0, hscale, vscale);
				yoffset += yspace;
			}
			else if(ENF_DIAG_BOOL(DM_STATS_FPS))
			{
				m_strFrameStats.format("Fps:%03.2lf (%03.2lfms)\0", g_fFPS, 1000.0f/enf_max(0.001f, (float) g_fFPS));
				m_pFont->RenderText( xoffset,  yoffset, m_strFrameStats.cstr(), m_strFrameStats.Length(), 0xffffffff, 0, hscale, vscale);
				yoffset += yspace + 5;
			}

			if(ENF_DIAG_VALUE(DM_STATS_STATS) & 2)
			{
				int p = 0;
				char tmp[256];

				struct Item
				{
					uint	YPos;
					float	Share;
				};

				//make space between stats/profile
				yoffset += yspace;
				pageYOffset = yoffset;
				
				int64 ctime = timeStamp;
				for(int n = 0; n < Profiler::MAX_SLOTS; n++)
				{
					const char* name = Profiler::GetName(n);
					if(name[0] == 0 || !Profiler::IsRecent(n, ctime))
						continue;

					FillMem(tmp, 255, ' ');
					tmp[255] = 0;
					CopyMem(tmp, name, strlen(name));

					float ftime = (float)Timer::Tick2Time(Profiler::GetFrameTicks(n), Timer::RESOLUTION_MILLISECONDS * 100) * 0.01f;
					float lftime = (float)Timer::Tick2Time(Profiler::GetLocalFrameTicks(n), Timer::RESOLUTION_MILLISECONDS * 100) * 0.01f;

					sprintf(tmp + 20, "%5d %6.2fms (%02.2f%%) | %6.2fms (%02.2f%%)", Profiler::GetCount(n),
						ftime, (float)Profiler::GetFrameRatio(n, 10000) / 100.0f,
						lftime, (float)Profiler::GetLocalFrameRatio(n, 10000) / 100.0f
						);
					m_pFont->RenderText( xoffset + 2,  yoffset, tmp, enf::strlen(tmp), 0xffffffff, 0, hscale, vscale);

					yoffset += yspace;
					
					if (yoffset > g_pRender->GetTargetHeight() - 12)
					{
						yoffset =  pageYOffset;
						xoffset += NEW_PAGE_X_OFFSET;
					}

					p++;
				}

				strcpy(tmp, "TOTAL FRAME TICKS                ");

				sprintf(tmp + 20, "    %6.2fms", (float)Timer::Tick2Time(Profiler::GetTotalFrameTicks(), Timer::RESOLUTION_MILLISECONDS * 100) * 0.01f);

				m_pFont->RenderText( xoffset + 2,  yoffset, tmp, enf::strlen(tmp), 0xffffffff, 0, hscale, vscale);
				yoffset += yspace;

				enableProfiler = true;
			}
			EngineStats::Reset();
			m_pRender->ResetStats();
			m_pFont->End();
		}
	}

	if(ENF_DIAG_VALUE(DM_STATS_SLOWFRAMES) > 0)
		enableProfiler = true;

	if(enableProfiler)
		Profiler::Enable();
	else
		Profiler::Disable();

	if(ENF_DIAG_BOOL(DM_STATS_COUNTER))
	{
		m_pRender->SetViewport(RenderViewport(0, 0, m_pRender->GetTargetWidth(), m_pRender->GetTargetHeight(), 0, 1.0f));

		if(m_pFont->Begin(FF_NOFILTER|FF_OPAQUE, NULL))
		{
			int p = 0;
			char tmp[256];

			//make space between stats/profile
			yoffset += yspace;

			if (!pageYOffset)
				pageYOffset = yspace;

			tmp[255] = 0;

			for(int n = 0; n < 256; n++)
			{
				CStr name = Profiler::GetCounterName(n);
				if(name.IsEmpty())
					continue;

				int nlength = name.Length();
				FillMem(tmp + nlength, 255 - nlength, ' ');
				CopyMem(tmp, name.cstr(), nlength);

				sprintf(tmp + 20, "%5d %8d", Profiler::GetCounterCount(n), Profiler::GetCounterTotalCount(n));
				m_pFont->RenderText( xoffset + 2,  yoffset, tmp, enf::strlen(tmp), 0xffffffff, 0, hscale, vscale);
				yoffset += yspace;

				if (yoffset > g_pRender->GetTargetHeight() - 12)
				{
					yoffset =  pageYOffset;
					xoffset += NEW_PAGE_X_OFFSET;
				}

				p++;
			}
			m_pFont->End();
		}
	}
	Profiler::BeginFrame();
#endif
}

//-----------------------------------------------------------------------------
void Engine::OnRender()
{
	if(m_pWidgetManager)
	{
#ifdef ENF_DONE
		m_pWidgetManager->RenderWidgets();
#endif
	}
}

//-----------------------------------------------------------------------------
// Name: RenderFrame()
// Desc: Draws the scene.
//-----------------------------------------------------------------------------
ERESULT Engine::RenderFrame(bool inscene)
{
#ifdef ENF_DONE
	if(m_pRender->IsLocked())
		return EE_ERROR;

	IL_INCREMENT(m_iFrameCount);

	if(ENF_DIAG_VALUE(DM_REND_RELOADSHADERS))
	{
		ENF_DIAG_SET(DM_REND_RELOADSHADERS, 0);
		LogF(LL_NORMAL, "Reloading shaders\n---------------------------------");
		SendEvent(ReloadShadersEvent());
	}

	if (ENF_DIAG_VALUE(DM_REND_OFFLINESHADERS))
	{
		GetRender()->GenerateOfflineShaders();
		ENF_DIAG_SET(DM_REND_OFFLINESHADERS, 0);
	}


	static int doScreenshot = 0;
	static RenderSetup setup;

	if(MainConfig::m_bEnableScreenshots)
	{
		if(doScreenshot)
		{
			if(--doScreenshot == 0)
			{
				char name[MAX_FILENAME_LENGTH];

				if(GetFileSystem()->GetFreeFileName("$profile:screen%03d.tga", name, 500))
				{
					VFile* vf = GetFileSystem()->Open(name, FILEMODE_WRITE);
					if(vf)
					{
						CFileOutputStream	outstream(vf);
						m_pRender->MakeScreenshot(NULL, &outstream);
					}
				}
				Resize(setup.m_iBackBufferWidth, setup.m_iBackBufferHeight, 0, setup.m_bWindowed, setup.m_PresentationInterval == 1);
			}
		}

		if(m_pInput->KeyState(KC_F7) && !doScreenshot)
		{
			m_pInput->KeyClear(KC_F7);

			if(m_pInput->KeyState(KC_LSHIFT) != 0 && MainConfig::m_bEnableSuperScreenshots)
			{
				doScreenshot = 3;
				setup = m_pRender->GetSetup();
				Resize(2560, 1440, 0, true, false);
			}
			else
			{
				#ifdef ENF_STEREO_VISION
					if (GetRender()->IsStereoEnabled())
					{
						GetRender()->CaptureStereoImage();
					}
					else
				#endif
					{
						char name[MAX_FILENAME_LENGTH];

						if(GetFileSystem()->GetFreeFileName("$profile:screen%03d.tga", name, 500))
						{
							VFile* vf = GetFileSystem()->Open(name, FILEMODE_WRITE);
							if(vf)
							{
								CFileOutputStream	outstream(vf);
								m_pRender->MakeScreenshot(NULL, &outstream);
							}
						}
					}
			}
		}

		if(m_pInput->KeyState(KC_F6))
		{
			m_pInput->KeyClear(KC_F6);
			g_bGrabVideo = !g_bGrabVideo;

			if(g_bGrabVideo)
			{
				if(!OpenVideo())
					g_bGrabVideo = false;
			}
			else
			{
				CloseVideo();
			}
		}

		if(g_bGrabVideo)
		{
			AppendVideoFrame(GetTime());
		}
	}

	
	if(!inscene)
	{
		m_pRender->Present();
	}

	for(uint mod = 0; mod < m_Modules.GetCardinality(); mod++)
	{
		m_Modules[mod]->OnBeginFrame();
	}

#if defined(ENF_LEAK_DETECTOR) && defined(WIN32)
	if(_GetAsyncKeyState(SysKey::Fn + 10) & 0x8000)
	{
		MemoryManager::Dump("\n\n Memory report \n", MD_NODATA|MD_NOSTACK);
	}
#endif
//	g_pvFTDelta->Float = (float)m_dTickDelta * 0.001f;

	ERESULT hres;

	if(!inscene)
	{
		hres = m_pRender->BeginScene();

		if(hres != EE_OK)
			return hres;
	}

	OnBeginScene();

	OnRender();

	SendEvent(AfterRenderEvent());

	uint slowMS = ENF_DIAG_VALUE(DM_STATS_SLOWDOWN);

	if(slowMS)
	{
		uint ms[] = {0,1,5,10,20,50,100};
		CThread::Sleep(ms[slowMS]);
	}

	TimeStamp timeStamp = Timer::GetTicks();
	
#ifdef ENF_DEVELOPER
	ProcessStatistics(timeStamp);
#endif

#ifdef ENF_ENABLE_DIAG
	ProcessDiagMenu(timeStamp);
#endif

	OnEndScene();
	if(!inscene)
	{
		m_pRender->EndScene();
	}

	FPS(timeStamp);
#endif
	return EE_OK;
}

//---------------------------------------------------------------------------
void Error2Log(const char *error)
{
#ifdef WIN32
	char cname[256];
	DWORD size = sizeof(cname);
	SYSTEMTIME syst;

	::GetComputerName(cname, &size);
	::GetLocalTime(&syst);


	MainConfig::m_CrashLog.Printf("\n\n------------------------------------\n%s, %02d.%02d %04d %02d:%02d:%02d\n"
		,cname, 
		(int)syst.wDay,
		(int)syst.wMonth,
		(int)syst.wYear,
		(int)syst.wHour,
		(int)syst.wMinute,
		(int)syst.wSecond);
#endif

	MainConfig::m_CrashLog << error;
	MainConfig::m_CrashLog << "\n\n";

	if(IsEngine())
	{
		Engine* eng = GetEngine();

		if(eng->GetRuntimeMode())
			MainConfig::m_CrashLog << "Runtime mode\n";
		else
			MainConfig::m_CrashLog << "Workbench mode\n";

		if(eng->GetNumParams())
		{
			MainConfig::m_CrashLog << "CLI params: ";

			for(uint n = 0; n < eng->GetNumParams(); n++)
			{
				const Engine::CLIParam& cp = eng->GetParam(n);
				DString str = cp.Key + " " + cp.Data + " ";

				MainConfig::m_CrashLog << str.cstr();
			}
			MainConfig::m_CrashLog << "\n";
		}
		GetEngine()->SendEvent(CrashLogEvent(MainConfig::m_CrashLog));
	}
}

//-----------------------------------------------------------------------
void Engine::FatalError(ErrorHandler::ErrorType type, const char* message)
{
	if(m_pErrorHandler)
	{
		m_pErrorHandler->HandleError(type, message);
	}
	else
	{
		if(MainConfig::m_bShowCrash)
		{
#ifdef WIN32
			::ShowCursor(true);
			::MessageBox(NULL, message, "Engine error", MB_OK|MB_ICONERROR|MB_SETFOREGROUND|MB_TOPMOST);
			::ShowCursor(false);
#elif defined(ENF_CONSOLE)
			_OutputDebugString(message);
			_DebugBreak();
#endif
		}
	}

	if(type != ErrorHandler::ET_INFO)
	{
		Destroy();
		exit(0);
	}
}

//-----------------------------------------------------------------------
const char* SkipSpaces(const char* str)
{
	while(*str == ' ' || *str == 9) str++;
	return str;
}

DString GetToken(const char*& str)
{
	DString res;
	str = SkipSpaces(str);
	const char* end = strchr(str, ' ');
	if(end == NULL)
	{
		end = str + strlen(str);
		if(*str == '\"')
		{
			res.Assign(str + 1, end - str - 2);
		}
		else
		{
			res = str;
		}
		str = end;
	}
	else
	{
		if(*str == '\"')
		{
			res.Assign(str + 1, end - str - 2);
		}
		else
		{
			res.Assign(str, end - str);
		}
		str = end;
	}

	return res;
}

//-----------------------------------------------------------------------
void Engine::ParseParams(const char* cliparams)
{
	while(*cliparams)
	{
		bool named = false;

		DString token = GetToken(cliparams);
		if(token.IsEmpty())
			break;

		if(token.At(0) == '-')
		{
			named = true;
			token = token.Right(token.Length() - 1);
		}

		if(named)
		{
			const char* tmp = cliparams;
			DString token2 = GetToken(cliparams);
			if(!token2.IsEmpty())
			{
				if(token2.At(0) == '-')
				{
					//another named
					cliparams = tmp;
				}
				else
				{
					m_Params.Insert(CLIParam(token, token2));
					continue;
				}
			}
		}
		m_Params.Insert(CLIParam(token, ""));
	}

	if(GetParam("alwaysupdate") != NULL ||
		GetParam("forceupdate") != NULL)
		m_bAlwaysUpdate = true;
}

//-----------------------------------------------------------------------
const char* Engine::GetParam(CStr param) const
{
	uint index = m_Params.GetIndexOf(CLIParam(param.cstr(), ""));

	if(index == INDEX_NOT_FOUND)
		return NULL;

	return (m_Params[index].Data.IsEmpty()) ? m_Params[index].Key.c_str() : m_Params[index].Data.c_str();
}

//-----------------------------------------------------------------------
void Engine::SetPaused(bool paused)
{
	if(m_bPaused != paused)
	{
		m_bPaused = paused;
		SendEvent(SetPausedEvent(paused));
	}
}

//-----------------------------------------------------------------------
void InternalError(const char *text,...)
{
	va_list argptr;
	char tmp1[32768];
	char tmp2[32768];

	sprintf(tmp1, "ERROR: %s",text);

	va_start (argptr, text);
	vsprintf (tmp2, tmp1, argptr);
	va_end (argptr);

	Error2Log(tmp2);
	if(GetEngine())
		GetEngine()->FatalError(ErrorHandler::ET_CRITICAL, tmp2);
	else
	{
#ifdef WIN32
		::ShowCursor(true);
		::MessageBox(NULL, tmp2, "Engine error", MB_OK|MB_ICONERROR|MB_SETFOREGROUND|MB_TOPMOST);
		::ShowCursor(false);
#else
		_OutputDebugString(tmp2);
#endif
		_DebugBreak();
	}
}

//-----------------------------------------------------------------------
void Error2(const char *type, const char *text,...)
{
va_list argptr;
char tmp2[32768];

	va_start (argptr, text);
	vsprintf (tmp2, text, argptr);
	va_end (argptr);
	Error2Log(tmp2);

	if(GetEngine())
		GetEngine()->FatalError(ErrorHandler::ET_CRITICAL, tmp2);
}

//-----------------------------------------------------------------------------
bool ScriptConsoleHandler::ProcessCommand(CParser& tp, ScriptModule* mod)
{
#ifdef ENF_DONE
	TToken ttemp;
	Variable* var;

	if((var = mod->GetVariable(tp.GetToken(0)->Text)) != NULL)
	{
		if(tp.GetLine(0).m_iNumTokens > 1)
		{
			if(var->GetVType() == VARTYPE_STRING && tp.GetToken(1)->Type != TT_QUOTE)
			{
				ttemp.Type = TT_QUOTE;
				ttemp.Spaces = 0;
				ttemp.Text = tp.GetToken(1)->Text;
				if(!var->Set(&ttemp, var))
				{
					LogF(LL_NORMAL, "Can't set variable");
					return true;
				}
			}
			else
			{
				if(!var->Set(tp.GetToken(1), var))
				{
					LogF(LL_NORMAL, "Can't set variable");
					return true;
				}
			}
		}

		char tmp[65536];
		Variable2Text(var, tmp, false, true);
		LogF(LL_NORMAL, tmp);
		return true;
	}

	Funct *fn;
	uint index;

	index = mod->GetFunctionIndex(tp.GetToken(0)->Text);
	if(index != INDEX_NOT_FOUND)
	{
		fn = mod->GetFunction(index);
		
		for(uint p = 1; p < tp.GetLine(0).m_iNumTokens && (p - 1) < fn->GetNumParameters(); p++)
		{
			var = fn->GetVariable(p - 1);

			if(var->GetVType() == VARTYPE_STRING && tp.GetToken(0)->Type != TT_QUOTE)
			{
				ttemp.Type = TT_QUOTE;
				ttemp.Spaces = 0;
				ttemp.Text = tp.GetToken(p)->Text;
				var->Set(&ttemp, var);
			}
			else
				var->Set(tp.GetToken(p), var);
		}
		mod->TCall(index + 1, NULL);
		return true;
	}
#endif
	return false;
	//LogF(LL_NORMAL, "No function/variable");
}

//-----------------------------------------------------------------------------
int ScriptConsoleHandler::HandleEvent(const Event& ev)
{
	if(!ev.HasType(ConsoleEventTypeID))
		return 0;

	const char* cmd = ((const ConsoleEvent&)ev).GetCmd();

	CParser tp;

	tp.Parse("console", cmd, strlen(cmd));

	if(tp.GetNumLines() == 0 || tp.GetLine(0).m_iNumTokens == 0)
		return 0;


	if(ProcessCommand(tp, m_pContext))
		return true;

	for(uint n = 0; n < m_pContext->GetNumModules(); n++)
	{
		if(ProcessCommand(tp, m_pContext->GetModule(n)))
			break;
	}

	return 0;
}

}


//-----------------------------------------------------------------------

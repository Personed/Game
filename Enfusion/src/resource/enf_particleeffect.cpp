//-----------------------------------------------------------------------------
// File: enf_particleeffect.cpp
//
// Desc: Particle effect & emitors
//
// Copyright (c) 2000-2010 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "enf_main.h"
#include "system/enf_profiler.h"
//#include "baseworld/enf_baseworld.h"
#include "baseworld/enf_baseworldtrace.h"
#include "baseworld/enf_entity.h"
//#include "BaseWorld/enf_baseworldwind.h"
//#include "BaseWorld/enf_worldphysics.h"
#include "pipeline/enf_pipeline.h"
#include "materials/enf_materialsystem.h"
#include "common/enf_parser.h"
#include "common/enf_serialize.h"
#include "common/enf_quicksort.h"
#include "common/enf_random.h"
#include "common/enf_diag.h"
#include "common/enf_simdmathlib.h"
#include "common/enf_streams.h"
#include "Render/enf_renderresources.h"
#include "materials/enf_materials.h"
#include "system/enf_threadmanager.h"
#include "resource/enf_particleeffectinternal.h"
#include "common/enf_iff.h"
#include "common/enf_compactarray.h"
#include "Physics/enf_worldphysics.h"
#include "render/enf_texture.h"
#include "engine/enf_engine.h"

namespace enf
{

#ifdef ENF_DONE
#define ENF_MT_PARTICLE
#endif

ParticleSystem* g_pParticleSystem = NULL;

int ParticleSystem::m_iLODDistances[] = {8192, 4096, 2048, 1024};

ENF_ALIGN16(Vector4)	ParticleSystem::m_vRandomVector[256];
uint ParticleSystem::m_iRandomVector = 0;

struct _WNode;

struct ParticleFileHeader
{
	ushort	NumParams;
	ushort	NumEmitors;
	//ParticleFileParam[NumParams] follows
	//ParticleFileEmitor[NumEmitors] follows
};

struct ParticleFileEmitor
{
	ushort	EmitorName;
	ushort	NumParams;
	//ParticleFileParam[NumParams] follows
};

struct ParticleFileParam
{
	ushort	ParamName; //Offset to string table
	ushort	NumElems;
	/*
	union
	{
		float	FloatValue;
		int	IntValue
		float	Vector3Value[3];
		float	Vector2Value[2];
		uint	String; //Offset to string table
	} [NumElems] follows
	*/
};

//-----------------------------------------------------------------------------
struct ParticleSystem::BillboardSort
{
	Vector4 m_vBillboard1;
	Vector4 m_vBillboard2;
	Vector3 m_vSort;
	float   m_fSortZ;
};

/*!
size 8 floats = 32 bytes
*/
struct StreakVert
{
	float		Origin[3];		///< origin
	float		MTime;			///< normalized master time of particle
	float		Time;				///< normalized time of particle
	float		Velocity[3];	///< velocity
};


/*!
size 6 floats = 24 bytes
*/
struct SpriteVert //32
{
	float		Origin[3];		///< origin
	float		MTime;			///< normalized master time of particle
	float		Time;				///< normalized time of particle
	float		Angle;			///< radians
};


/*!
size 5 floats = 20 bytes
*/
struct PSpriteVert
{
	float		Origin[3];	///< origin
	float		MTime;		///< normalized master time of particle
	float		Time;			///< normalized time of particle
};

//-----------------------------------------------------------------------------
struct Particle
{
	//2x16 = 32
	Vector3	m_vPosition;
	float		m_fTimeScale;
	Vector3	m_vVelocity;
	float		m_fAngle;

	//4x4 = 16
	float		m_fGravityScale;
	float		m_fAirResistance;
	float		m_fAngVel;
	float		m_fMTime;	//birth time of emitor
	//total 48byte
};

//-----------------------------------------------------------------------------
struct PEmitor
{
	ENF_DECLARE_SMALLOBJECT(PEmitor);

	uint				m_iNumActiveParticles;
	Particle*		m_pParticles;
	float*			m_pParticleTimes;
	uint				m_iMaxParticles;
	uint				m_iTop, m_iBottom;
	
	float				m_fEmitTime;
	float				m_fTime;

//parms copied from EmitorDef
	uint				m_iTexIndex;
	float				m_fVelocity;
	float				m_fVelocityRND;
	Vector3			m_vEmitOffsets;
	Vector3			m_vEmitAngles;

	Vector3			m_vEmitAngle;
	float				m_fGravityMult;
	float				m_fGravityMultRND;
	float				m_fSize;
	float				m_fStretch;
	float				m_fLifetime;
	float				m_fLifetimeRND;
	float				m_fBirthRate;
	float				m_fBirthRateRND;
	float				m_fBirthRateVel;
	float				m_fAngleVelMult;
	float				m_fAirResistance;
	float				m_fAirResistanceRND;
	float				m_fEmitorTime;
	float				m_fSpring;
	uint				m_iFlags;
	PEmitor*			m_pNext;

	ENF_INLINE void EmitNew(Particle* part, EmitorDef::EmitorType def, const SimdMatrix34& mat, const SimdVector& emitShape, const SimdVector& pos, const SimdVector& prev, const SimdVector& lerp);
	//ENF_INLINE void EmitNew(Particle* ptr, const EmitorDef* def, const Matrix43& mat, const Vector3& pos, const Vector3& prev, float lerp);

	ENF_INLINE void KillParticle(uint n);
	//!Returns true if effect reached its end
	bool Update(BaseWorld* world, const EmitorDef* def, ParticleEffectInstance* ai, const Matrix43& mat, float timeslice, AAB& bbox, uint flags, const struct LocalWindEmitter* emitters, uint numEmitters, const Vector3& vel, MTrace& trace, _WNode* node);
	void Restart(const EmitorDef* def, ParticleEffectInstance* ai);

	PEmitor(uint maxparticles);
	~PEmitor();

	//disable batching
	ENF_INLINE void DisableBatching()
	{
		m_iTexIndex = 0xFFFFFFFF;
	}

	ENF_INLINE bool IsBatchingDisabled() const
	{
		return m_iTexIndex == 0xFFFFFFFF;
	}
};


/*!
index type for emitor texture graphs
*/
//-----------------------------------------------------------------------------
class EmitorTextureGraph 
{
public:
	static const uint EMIT_ROWS    = 3;
	static const uint MAXINDICES   = PARAM_TEXTURE_SIZE / EMIT_ROWS;

protected :
	/*!
	depends on MAXINDICES and type of EmitorIndex
	*/
	static const TexGraphIndex UNPACK_MASK  = 0xFF;
	static const TexGraphIndex UNPACK_SHIFT = 8;

	/*!
	one texture container
	*/
	struct TextureContainer
	{
		ComPtr<Texture> m_pTexture;
		SArray<TexGraphIndex, MAXINDICES> m_EmptyIndices;
		
		ENF_INLINE void* LockRow(uint index, uint ioffs);
		ENF_INLINE void Unlock();
		
		TextureContainer();
		ENF_INLINE void AllocTexture();
	};

	Array<TextureContainer*> m_Textures;

	/*!
	unpack just texture
	*/
	ENF_INLINE uint UnpackTexIndex(TexGraphIndex index)
	{
		return index >> UNPACK_SHIFT;
	}

	/*!
	unpack EmitorIndex to texture index and row index
	*/
	ENF_INLINE uint UnpackIndex(TexGraphIndex& index)
	{
		uint tex = index >> UNPACK_SHIFT;
		index = index & UNPACK_MASK;
		return tex;
	}

	/*!
	pack
	*/
	ENF_INLINE TexGraphIndex PackIndex(uint texnum, TexGraphIndex rowindex)
	{
		return (texnum << UNPACK_SHIFT) | rowindex;
	}

	/*!
	get empty index
	*/
	ENF_INLINE uint GetEmptyIndex(TexGraphIndex& index);

public:
	~EmitorTextureGraph();

	/*!
	fill from graphs into texture
	\param index	index of data, if is INDEX_NOT_FOUND, new rows are allocated in texture and its index is returned
	\param ioffs	line offset
	\param row		array of data
	\return index of data in texture
	*/
	TexGraphIndex FillData(TexGraphIndex index, uint ioffs, const void* row);

	/*!
	release index
	*/
	ENF_INLINE void ReleaseIndex(TexGraphIndex &index);

	/*!
	return texture with parameters
	*/
	Texture* GetParamTexture(TexGraphIndex index)
	{
		return m_Textures[UnpackTexIndex(index)]->m_pTexture;
	}

	/*!
	GPU index of texture -> u texture coordinate
	*/
	float GetGPUIndex(TexGraphIndex index)
	{	
		return enf_limit((float) (EMIT_ROWS*(index & UNPACK_MASK))/(float) PARAM_TEXTURE_SIZE, 0.0f, 0.9999f);
	}

}; //TODO remove this

//---------------------------------------------------------------------------
template<> void PGraph<float,PART_GRAPH_SIZE>::Fill(float start, float end)
{
	for(int n = 0; n < PART_GRAPH_SIZE; n++)
		m_Graph[n] = enf_lerp(start, end, (float)n / PART_GRAPH_SIZE);
}

//---------------------------------------------------------------------------
template<> void PGraph<FColor,PART_GRAPH_SIZE>::Fill(FColor start, FColor end)
{
	for(int n = 0; n < PART_GRAPH_SIZE; n++)
	{
		float frac = (float)n / PART_GRAPH_SIZE;
		m_Graph[n] = start.Lerp(end, frac);
	}
}

//-----------------------------------------------------------------------------
template<> ENF_INLINE float PGraph<float, PART_GRAPH_SIZE>::GetValue(uint pos, float frac) const
{
	return enf_lerp(m_Graph[pos + 0], m_Graph[pos + 1], frac);
}

//-----------------------------------------------------------------------------
template<> FColor PGraph<FColor, PART_GRAPH_SIZE>::GetValue(uint pos, float frac) const
{
	return m_Graph[pos].Lerp(m_Graph[pos + 1], frac);
}

//-----------------------------------------------------------------------------
Texture* ParticleSystem::GetParamTexture(TexGraphIndex index)
{
	return m_pTextureGraphs->GetParamTexture(index);
}

//-----------------------------------------------------------------------------
ENF_ALIGN16(class) SimdRandom
{
	#ifdef ENF_SSE
		ENF_ALIGN16(__m128i) m_CurrSeed;
	#endif

public:
	void Seed(int seed)
	{
		#ifdef ENF_SSE
			m_CurrSeed = _mm_set_epi32(seed, seed ^ 0x63151799, seed ^ 0x7f247a5c, seed ^ 0xe3ca35c3);
		#endif
	}

	SimdRandom()
	{
		#ifdef ENF_SSE
			Seed(23456);
		#endif
	}

	SimdVector RandFloats()
	{
		#ifdef ENF_SSE
			static const ENF_ALIGN16(unsigned int) mult[4] = { 214013, 214013, 214013, 214013 }; 
			static const ENF_ALIGN16(unsigned int) gadd[4] = { 2531011, 2531011, 2531011, 2531011 }; 
			static const ENF_ALIGN16(unsigned int) mask[4] = { 0xFFFFFFFF, 0, 0xFFFFFFFF, 0 };
			static const ENF_ALIGN16(unsigned int) masklo[4] = { 0x00007FFF, 0x00007FFF, 0x00007FFF, 0x00007FFF }; 

			__m128i adder = _mm_load_si128( (__m128i*) gadd);
			__m128i multiplier = _mm_load_si128( (__m128i*) mult);
			__m128i mod_mask = _mm_load_si128( (__m128i*) mask);
			__m128i cur_seed = m_CurrSeed;
			__m128i cur_seed_split = _mm_shuffle_epi32( cur_seed, _MM_SHUFFLE( 2, 3, 0, 1 ) );
		
			cur_seed = _mm_mul_epu32( cur_seed, multiplier ); 
			multiplier = _mm_shuffle_epi32( multiplier, _MM_SHUFFLE( 2, 3, 0, 1 ) );
			cur_seed_split = _mm_mul_epu32( cur_seed_split, multiplier );
			cur_seed = _mm_and_si128( cur_seed, mod_mask);
			cur_seed_split = _mm_and_si128( cur_seed_split, mod_mask );
			cur_seed_split = _mm_shuffle_epi32( cur_seed_split, _MM_SHUFFLE( 2, 3, 0, 1 ) );
			cur_seed = _mm_or_si128( cur_seed, cur_seed_split );
			cur_seed = _mm_add_epi32( cur_seed, adder);
			m_CurrSeed = cur_seed;

			__m128i sseresult = _mm_srai_epi32( cur_seed, 16); 
			__m128i sra_mask = _mm_load_si128( (__m128i*) masklo);
			sseresult = _mm_and_si128( sseresult, sra_mask ); 
			static const SimdVectorF32 scale = {3.0518509475997e-5f,3.0518509475997e-5f,3.0518509475997e-5f,3.0518509475997e-5f};
		
			SimdVector rand01 = SimdVector(scale.v) * SimdVector(_mm_cvtepi32_ps(sseresult));

			return rand01 + rand01 - SimdVector::One();
		#else
			static ENF_ALIGN16(Vector4) vec;
			vec.x = RandNumber::RandFloat11();
			vec.y = RandNumber::RandFloat11();
			vec.z = RandNumber::RandFloat11();
			vec.w = RandNumber::RandFloat11();
			return SimdVector::LoadFloat4A16(vec.v);
		#endif
	}

	SimdVector RandVector()
	{
		//<0..1>
		SimdVector values = RandFloats();
		SimdVector save = values;

		//normalize x,y,z
 		SimdVector norm = values.QNormalize3();

		//multiply normalized vector by random value from W, the sign of W is unimportant
		SimdVector randSph = norm*save.SplatW();

		return randSph;
	}
};


//-----------------------------------------------------------------------------
void ParticleSystem::BeginRender(Pipeline* pipeline)
{
	m_iAllocIndex = pipeline->AllocObject();
	m_pEffectsToRender.Clear();
}

//-----------------------------------------------------------------------------
void ParticleSystem::OnBeginFrame()
{
	if (!m_iSkipNextFrame)
	{
		m_iSkipNextFrame--;
	}
	else
	{
		m_iVB++;

		if(m_iVB >= NUM_PARTBUFFERS)
			m_iVB = 0;
	}

	m_pNewSpriteVB[m_iVB]->Reset();
	m_pNewPointSpriteVB[m_iVB]->Reset();
	m_pNewStreakVB[m_iVB]->Reset();
}

//-----------------------------------------------------------------------------
ParticleSystem::~ParticleSystem()
{
	SAFE_DELETE(m_pTextureGraphs);

	//clear current pointer
	if(this == g_pParticleSystem)
		g_pParticleSystem = NULL;
}

//-----------------------------------------------------------------------------
void ParticleSystem::OnInsert(TiXmlElement* settings)
{
	enf_assert2(GetEngine()->GetRender(), "Renderer device must be inserted prior to ParticleSystem");
	
	for(uint n = 0; n < NUM_PARTBUFFERS; n++)
	{
		m_pNewPointSpriteVB[n] = g_pRender->CreateVertexBuffer(RES_USAGE_POINTS|RES_USAGE_DYNAMIC|RES_USAGE_WRITEONLY, (PARTICLE_POINT_VB_SIZE + 1), sizeof(PSpriteVert), "PointSpritesParticles");
		m_pNewSpriteVB[n] = g_pRender->CreateVertexBuffer(RES_USAGE_DYNAMIC|RES_USAGE_WRITEONLY, (PARTICLE_MAX_SPRITES + 1)*sizeof(SpriteVert), 1, "SpritesParticles");
		m_pNewStreakVB[n] = g_pRender->CreateVertexBuffer(RES_USAGE_DYNAMIC|RES_USAGE_WRITEONLY, (PARTICLE_MAX_STREAKS + 1)*sizeof(StreakVert), 1, "StreaksParticles");
	}
}



//-----------------------------------------------------------------------------
ParticleSystem::ParticleSystem(): m_iVB(0), m_iSkipNextFrame(0), m_pInSimulation(NULL)
{
	//texture manager
	m_pTextureGraphs = ENF_NEW EmitorTextureGraph;

	for(uint n = 0; n < NUM_PARTBUFFERS; n++)
	{
		m_pNewSpriteVB[n] = NULL;
		m_pNewPointSpriteVB[n] = NULL;
		m_pNewStreakVB[n] = NULL;
	}

	ENF_PROFILER_SETCOUNTER (PROFILE_PART_BATCHES, "Particle batches");
	ENF_PROFILER_SETCOUNTER (PROFILE_PART_EMITTERS, "Particle emitters");

	ENF_PROFILER_SETCOUNTER (PROFILE_PART_SPRITES, "Particle sprites left");
	ENF_PROFILER_SETCOUNTER (PROFILE_PART_STREAKS, "Particle streaks left");
	ENF_PROFILER_SETCOUNTER (PROFILE_PART_POINTS, "Particle points left");

	for(uint n = 0; n < 256; n++)
	{
		float angle = RandNumber::RandFloat0X(ENF_PI2);
		float elev = RandNumber::RandFloat0X(ENF_PI2);
		float c = cosf(elev);
		m_vRandomVector[n].x = c * cosf(angle);
		m_vRandomVector[n].y = c * sinf(angle);
		m_vRandomVector[n].z = sinf(elev);
		m_vRandomVector[n].w = RandNumber::RandFloat01();
	}
}

//-----------------------------------------------------------------------------
void ParticleSystem::BeginSimulation(BaseWorld* world)
{
	enf_assert(m_pInSimulation == NULL);
	m_pInSimulation = world;
}
//-----------------------------------------------------------------------------
void ParticleSystem::EndSimulation(BaseWorld* world)
{
	enf_assert(m_pInSimulation == world);
	if(!m_EffectsToSimulate.IsEmpty())
	{
		#ifdef ENF_DONE
			GetEngine()->GetThreadManager(1)->WaitForIdle();
		#endif

		for(uint n = 0; n < m_EffectsToSimulate.GetCardinality(); n++)
		{
			ParticleEffectInstance* pe = m_EffectsToSimulate[n].Key;
		
#ifdef ENF_DONE
				Entity* ent = m_EffectsToSimulate[n].Data;
#endif

			if(IL_COMPARE_EXCHANGE(pe->m_eState, SS_IDLE, SS_SUBMITED) == SS_SUBMITED)
			{
#ifdef ENF_DONE
					ent->SetFlags(TFL_UPDATE);
#endif
			}
			else
			{
				delete pe;
			}
		}
		m_EffectsToSimulate.Clear();
	}
	m_pInSimulation = NULL;
}

//-----------------------------------------------------------------------------
void EmitorDef::DestroyEmitor(PEmitor *emitor)
{
	emitor->m_pNext = m_pDeletedEmitors;
	m_pDeletedEmitors = emitor;
}

//-----------------------------------------------------------------------------
PEmitor *EmitorDef::CreateEmitor(bool init)
{
	PEmitor *em;
	if(m_pDeletedEmitors)
	{
		em = m_pDeletedEmitors;
		m_pDeletedEmitors = em->m_pNext;
		em->m_pNext = NULL;
	}
	else
	{		
		em = ENF_NEW PEmitor(m_iMaxParticles);
	}

	if(init)
	{
		//initialize
		em->m_iNumActiveParticles = 0;
		em->m_iTop = em->m_iBottom = 0;
		em->m_fEmitTime = 0.0f;
		em->m_fTime = 0.0f;
		em->m_iMaxParticles = m_iMaxParticles;

		//copy instanced defs
		em->m_iTexIndex = m_iTexIndex;
		em->m_fVelocity = m_fVelocity;
		em->m_fVelocityRND = m_fVelocityRND;
		em->m_vEmitOffsets = m_vEmitOffsets;
		em->m_vEmitAngles = m_vEmitAngles;
		em->m_vEmitAngle = m_vEmitAngle;
		em->m_fGravityMult = m_fGravityMult;
		em->m_fGravityMultRND = m_fGravityMultRND;
		em->m_fSize = m_fSize;
		em->m_fStretch = m_fStretch;
		em->m_fLifetime = m_fLifetime;
		em->m_fLifetimeRND = m_fLifetimeRND;
		em->m_fBirthRate = m_fBirthRate;
		em->m_fBirthRateRND = m_fBirthRateRND;
		em->m_fBirthRateVel = m_fBirthRateVel;
		em->m_fAngleVelMult = m_fAngleVelMult;
		em->m_fAirResistance = m_fAirResistance;
		em->m_fAirResistanceRND = m_fAirResistanceRND;
		em->m_iFlags = m_iFlags;
		em->m_fEmitorTime = m_fEmitorTime;
		em->m_fSpring = m_fSpring;
	}

	return em;
}

//-----------------------------------------------------------------------------
EmitorDef::EmitorDef():
	m_fRestitution(0),
	m_fLifetimeShortening(0),
	m_pMaterial(NULL),
	m_iSortID(0),
	m_iTexIndex(TEXGRAPH_BAD_INDEX),
	m_pAngleVelGraph(NULL),
	m_pMAngleVelGraph(NULL),
	m_pMBirthRateGraph(NULL),
	m_fAngleVelConstant(1.0f),
	m_fMAngleVelConstant(1.0f),
	m_fMBirthRateConstant(1.0f),
	m_pDeletedEmitors(NULL)
{
}

//-----------------------------------------------------------------------------
EmitorDef::~EmitorDef()
{
	if (g_pParticleSystem)
		g_pParticleSystem->GetTextureGraph()->ReleaseIndex(m_iTexIndex);

	SAFE_DELETE(m_pAngleVelGraph);
	SAFE_DELETE(m_pMAngleVelGraph);
	SAFE_DELETE(m_pMBirthRateGraph);

	PEmitor *next, *emit = m_pDeletedEmitors;
	while(emit)
	{
		next = emit->m_pNext;		
		delete emit;
		emit = next;
	}
}

enum PartType
{
	PT_SPRITE,
	PT_POINT,
	PT_STREAK
};

//---------------------------------------------------------------------------
/*
void ParticleEffect::Flush(unsigned int level)
{
	for(uint n = 0; n < m_iNumEmitorDefs; n++)
	{
		PEmitor *next, *emit = m_pEmitorDefs[n].m_pDeletedEmitors;
		while(emit)
		{
			next = emit->m_pNext;		
			delete emit;
			emit = next;
		}
		m_pEmitorDefs[n].m_pDeletedEmitors = NULL;
	}
}
*/
//---------------------------------------------------------------------------
ENF_INLINE PartType PType(const EmitorDef* def)
{
	if(def->m_iFlags & PEF_POINTSPRITE)
		return def->m_iFlags & PEF_STRETCH ? PT_STREAK : PT_POINT;

	return PT_SPRITE;
}

//-----------------------------------------------------------------------------
void EmitorDef::Optimize()
{
	//optimize for point emitor
	if(m_vEmitShape.IsZero())
		m_eEmitorType = EmitorDef::POINT;

	//half radius
	m_vEmitShape *= 0.5f;

	//recompute stretch to usable value
	if (m_iFlags & PEF_POINTSPRITE && m_fStretch > 1.0f)
	{
		m_iFlags |= PEF_STRETCH;
		m_fStretch = -(1.0f - m_fStretch) * (1.0f / 32.0f);
	}

	if (!(m_iFlags & PEF_POINTSPRITE))
	{
		m_fSize *= 0.5f;
	}

//	if(m_iFlags & PEF_POINTSPRITE && m_fSize < 4)
//		m_iLOD = 3;
	
	m_fAngleVelMult *= ENF_PI / 180.0f;
	
	m_vEmitAngle.x = DEG2RAD(m_vEmitAngle.x);
	m_vEmitAngle.y = DEG2RAD(m_vEmitAngle.y) * 0.5f;
	m_vEmitAngle.z = DEG2RAD(m_vEmitAngle.z) * 0.5f;

	if(m_vEmitAngle.y < m_vEmitAngle.z)
		enf_swap(m_vEmitAngle.y, m_vEmitAngle.z);

	if((m_fAirResistance + m_fAirResistanceRND) == 0.0f)
		m_iFlags |= PEF_NO_AIR_RESIST;

	if(m_fEmitorTime == 0)
		m_fEmitorTime = 1.0f;

	if(m_fAnimFPS == 0)
		m_fAnimFPS = 1.0f;

	uint tx, ty;
	m_pMaterial->GetMaterialClass()->GetTilesCount(m_pMaterial, tx, ty);
	m_iTilesCount = tx*ty;

	if(m_iTilesCount > 1)
	{
		if ((m_iFlags & (PEF_ANIMATE | PEF_RANDOM_FRAME)) == 0)
		{
			m_iFlags |= PEF_ANIMATE;
		}

		if (m_iFlags & PEF_RANDOM_FRAME)
		{
			m_iFlags &= ~(PEF_LIFETIME_BY_ANIM | PEF_ANIM_ONCE);

			if (m_fLifetime < 0.001)
			{
				m_fLifetime = 1.0f;
			}
		}
	}
	else
		m_iFlags &= ~(PEF_LIFETIME_BY_ANIM | PEF_RANDOM_FRAME);

	m_iFlags |= m_vEmitOffsets.IsZero() ? 0 : PEF_OFFSET;
	m_iFlags |= m_vEmitAngles.IsZero() ? 0 : PEF_ANGLES;

	//rotation optimizations
	if (m_iFlags & PEF_VEL_ANGLE)
	{
		m_iFlags |= PEF_NO_ROTATION|PEF_NO_ANGULAR_VEL|PEF_NO_ANGULAR_VEL_GRAPH;
	}
	else
	{
		if(m_fAngleVelMult != 0.0f)
		{
			m_iFlags &= ~PEF_NO_ANGULAR_VEL;
			if(m_pAngleVelGraph == NULL)
				m_iFlags |= PEF_NO_ANGULAR_VEL_GRAPH;
			else
				m_iFlags &= ~PEF_NO_ANGULAR_VEL_GRAPH;
		}
		else
			m_iFlags |= PEF_NO_ANGULAR_VEL|PEF_NO_ANGULAR_VEL_GRAPH;

		if(m_iFlags & PEF_RANDOM_ANGLE || (m_fAngleVelMult != 0.0f))
			m_iFlags &= ~PEF_NO_ROTATION;
		else
			m_iFlags |= PEF_NO_ROTATION;
	}
}

//-----------------------------------------------------------------------------
PEmitor::PEmitor(uint maxparticles): m_iMaxParticles(maxparticles), m_pNext(NULL)
{
	m_pParticles = (Particle *)ALLOC16(sizeof(Particle) * maxparticles);
	m_pParticleTimes = ENF_NEW float[maxparticles];

	uint allocated = sizeof(Particle) * maxparticles;
	allocated += sizeof(float) * maxparticles;
	allocated += sizeof(*this);
	IL_EXCHANGE_ADD(MemStats::m_iParticles, allocated);
}

//-----------------------------------------------------------------------------
PEmitor::~PEmitor()
{
	FREE16(m_pParticles);
	SAFE_DELETE_ARRAY(m_pParticleTimes);
	
	int allocated = sizeof(Particle) * m_iMaxParticles;
	allocated += sizeof(float) * m_iMaxParticles;
	allocated += sizeof(*this);
	IL_EXCHANGE_ADD(MemStats::m_iParticles, -allocated);
}

//-----------------------------------------------------------------------------

void PEmitor::EmitNew(Particle* part, EmitorDef::EmitorType def, const SimdMatrix34& mat, const SimdVector& emitShape, const SimdVector& pos, const SimdVector& prev, const SimdVector& lerp)
{
	static SimdRandom rnd;

	SimdVector npos = pos.Lerp(prev, lerp);
	SimdVector rpos;

	switch(def)
	{
	case EmitorDef::POINT:
		rpos = npos;
		break;

	case EmitorDef::BOX:
		{
			SimdVector randomVec = rnd.RandFloats();
			rpos = npos + mat.TransformVector(randomVec * emitShape);
			
	//		ENF_ALIGN16(float) emitShapef[4];
	//		emitShape.StoreFloat4A16(emitShapef);
	//		rpos = npos + mat.TransformVector(SimdVector(RandNumber::RandFloatXX(emitShapef[0]),RandNumber::RandFloatXX(emitShapef[1]),RandNumber::RandFloatXX(emitShapef[2]),0.0f));

			break;
		}

	case EmitorDef::SPHERE:
	case EmitorDef::ELIPSE:
		{
			SimdVector randomVec = rnd.RandVector();
			rpos = npos + mat.TransformVector(randomVec * emitShape);

			break;
		}
	}
	rpos.StoreFloat4A16(part->m_vPosition.v);
}
/*
void PEmitor::EmitNew(Particle* part, const EmitorDef* def, const Matrix43& mat, const Vector3& pos, const Vector3& prev, float lerp)
{
	Vector3 npos = pos.Lerp(prev, lerp);

	switch(def->m_eEmitorType)
	{
	case EmitorDef::POINT:
		part->m_vPosition = npos;
		break;

	case EmitorDef::BOX:
		part->m_vPosition = mat.TransformVector(Vector3(
					RandNumber::RandFloatXX(def->m_vEmitShape.x),
					RandNumber::RandFloatXX(def->m_vEmitShape.y),
					RandNumber::RandFloatXX(def->m_vEmitShape.z))) + npos;
		break;

	case EmitorDef::SPHERE:
		{
			Vector4 vec = ParticleSystem::RandVector();
			part->m_vPosition = Vector3(vec.v) * (vec.w * def->m_vEmitShape[0]) + npos;
			break;
		}

	case EmitorDef::ELIPSE:
		{
			Vector4 vec = ParticleSystem::RandVector();
			Vector3 scale;
			scale.x = RandNumber::RandFloat0X(def->m_vEmitShape.x);
			scale.y = RandNumber::RandFloat0X(def->m_vEmitShape.y);
			scale.z = RandNumber::RandFloat0X(def->m_vEmitShape.z);
			part->m_vPosition = scale.Multiply(vec.v) + npos;
			break;
		}
	}
}
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC Vector3 Reflect(const Vector3& vec, const Vector3& plane, float scale)
{
	float backoff = plane.Dot(vec) * scale;

	return Vector3(vec.x - plane.x * backoff, vec.y - plane.y * backoff, vec.z - plane.z * backoff);
}

#define ISDEAD(p) (((*(uint*)&p) & 0x80000000) != 0)

//-----------------------------------------------------------------------------
void PEmitor::KillParticle(uint n)
{
	m_iNumActiveParticles--;
	m_pParticleTimes[n] = -1;
	uint bottom = m_iBottom;
	//shift bottom
	if(n == bottom)
	{
		do
		{
			if(++bottom >= m_iMaxParticles)
				bottom = 0;
			if(!ISDEAD(m_pParticleTimes[bottom]))
				break;
		} while(bottom != m_iTop);

		m_iBottom = bottom;
	}
}

//-----------------------------------------------------------------------------
struct LocalWindEmitter
{
	Vector3	pos;
	float		sqRadius;
	uint		index;
};

//-----------------------------------------------------------------------------
ENF_INLINEFUNC uint GetLocalWindEmitters(const Vector3& pos, const LocalWindEmitter* emitters, uint numEmitters)
{
	uint mask = 0;
	for(uint n = 0; n < numEmitters; n++)
	{
		if(pos.SquaredDistance(emitters[n].pos) < emitters[n].sqRadius)
			mask |= 1 << emitters[n].index;
	}
	return mask;
}

//-----------------------------------------------------------------------------
SimdMatrix34 MultiplyAdd(const SimdMatrix34& value, const SimdVector& mul, const SimdMatrix34& add)
{
	return SimdMatrix34(
		value.vec[0].MultiplyAdd(mul, add.vec[0]),
		value.vec[1].MultiplyAdd(mul, add.vec[1]),
		value.vec[2].MultiplyAdd(mul, add.vec[2]));
}

static const SimdVector partMask[] = {
	SimdVector(0x00000000, 0xffffffff, 0xffffffff, 0xffffffff),
	SimdVector(0x00000000, 0x00000000, 0xffffffff, 0xffffffff),
	SimdVector(0x00000000, 0x00000000, 0x00000000, 0xffffffff),
	SimdVector(0x00000000, 0x00000000, 0x00000000, 0x00000000)
};

SimdVector PartMin(const SimdMatrix34& m, const SimdVector& mask)
{
	SimdMatrix34 out;
	out.vec[0] = m.vec[0].Select(m.vec[0].SplatX(), mask);
	out.vec[1] = m.vec[1].Select(m.vec[1].SplatX(), mask);
	out.vec[2] = m.vec[2].Select(m.vec[2].SplatX(), mask);
	return out.Min();
}
SimdVector PartMax(const SimdMatrix34& m, const SimdVector& mask)
{
	SimdMatrix34 out;
	out.vec[0] = m.vec[0].Select(m.vec[0].SplatX(), mask);
	out.vec[1] = m.vec[1].Select(m.vec[1].SplatX(), mask);
	out.vec[2] = m.vec[2].Select(m.vec[2].SplatX(), mask);
	return out.Max();
}

//-----------------------------------------------------------------------------
bool PEmitor::Update(BaseWorld* world, const EmitorDef* def, ParticleEffectInstance* ai, const Matrix43& mat, float timeslice, AAB& bbox, uint flags, const LocalWindEmitter* emitters, uint numEmitters, const Vector3& vel, MTrace& trace, _WNode* node)
{
	uint eflags = m_iFlags;
	ENF_ALIGN16(Vector3) pos = (eflags & PEF_OFFSET) ? mat.TransformCoord(m_vEmitOffsets) : mat.GetTranslation();

	Matrix33 rmat;
	if((eflags & PEF_ANGLES) != 0)
	{
		Matrix33 lmat;
		lmat.RollPitchYaw(m_vEmitAngles[ROLL], m_vEmitAngles[PITCH], m_vEmitAngles[YAW]);
		rmat = mat.GetOrientation() * lmat;
	}
	else
		rmat = mat.GetOrientation();

	uint windemitters = 0;

#ifdef ENF_DONE
	WindInfo windinfo;
	if(eflags & PEF_WIND && ENF_DIAG_RBOOL(DM_DISABLE_WIND))
	{
		windemitters = GetLocalWindEmitters(pos, emitters, numEmitters);
		
		world->WindPrepare(windinfo);
		//hack, to have more power in direct impact from entities than the global wind
		windinfo.m_fWindSpeed *= WindPartGlobWind;
	}
#endif

	bool dospring = m_fSpring != 0.0f;
	float spring = 0.0f;
	float pdot = 0.0f;
	if(dospring)
	{
		spring = enf_limit((1.0f / (1.0f + timeslice)) * m_fSpring, 0.0f, 1.0f);
		pdot = rmat.GetRow(2).Dot(pos);
	}
	
	if(m_iBottom != m_iTop)
	{
		SimdVector radius(m_fSize * 0.5f);
		SimdVector gravity(-9.81f * 32.0f * timeslice);
		SimdVector vecTimeSlice(timeslice);
		SimdVector sphereCoordMin(bbox.GetMins().x, bbox.GetMins().y, bbox.GetMins().z, bbox.GetMins().z);//(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX); 
		SimdVector sphereCoordMax(bbox.GetMaxs().x, bbox.GetMaxs().y, bbox.GetMaxs().z, bbox.GetMaxs().z);//(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
	
		uint currItem = m_iBottom;
		while(currItem != m_iTop)
		{
			uint validIdx = 0;
			ENF_ALIGN16(float) frac[4];
			Particle *part[4];							//Keep particle pointers because of future new settings
			uint selItems[4];								//indices of particles used(for future info extraction)

			//extract up to 4 particles for one vector
			while(validIdx < 4 && currItem != m_iTop)
			{
				if(!ISDEAD(m_pParticleTimes[currItem]))
				{
					part[validIdx] = &m_pParticles[currItem];
					frac[validIdx] = m_pParticleTimes[currItem] + (timeslice * part[validIdx]->m_fTimeScale);
					if(frac[validIdx] > 1.0f)
					{
						KillParticle(currItem);
					}
					else
					{
						//this particle passed and can be added to vector
						selItems[validIdx] = currItem;
						++validIdx;
					}
				}
				++currItem;
				currItem = (currItem < m_iMaxParticles) ? currItem : 0;
			}

			//do not continue in case there is no particle
			if(validIdx == 0)
			{
				break;
			}

			//prefetch next 4 particles
			uint prefetchIdx = currItem;
			if(prefetchIdx != m_iTop)
			{
				ENF_PREFETCH(&m_pParticles[prefetchIdx]);
				ENF_PREFETCH((void*)(((intptr_t)&m_pParticles[prefetchIdx] & ~0x7f) + 128));
			}


			//fill information about particles to be proceed
			SimdVector partVel[4], partPos[4];
			ENF_ALIGN16(float) partGravity[4], partAirRes[4];
			for(uint i = 0; i < validIdx; ++i)
			{
				partVel[i] = SimdVector::LoadFloat4A16(part[i]->m_vVelocity.v);
				partPos[i] = SimdVector::LoadFloat4A16(part[i]->m_vPosition.v);
				partGravity[i] = part[i]->m_fGravityScale;
				partAirRes[i] = part[i]->m_fAirResistance;
			}
			//all pointers has to be valid! we know only that there is at least first particle valid
			for(uint i = validIdx; i < 4; ++i)
			{
				part[i] = part[0];
			}

			//time, position and fraction to graphs
			SimdVector ntime = SimdVector::LoadFloat4A16(frac);
			
			SimdMatrix34 vecVel = SimdMatrix34(partVel[0], partVel[1], partVel[2], partVel[3]);
			SimdMatrix34 vecPPos = SimdMatrix34(partPos[0], partPos[1], partPos[2], partPos[3]);
			
			//WIND test
#ifdef ENF_DONE
			if(eflags & PEF_WIND && ENF_DIAG_RBOOL(DM_DISABLE_WIND))
			{
				SimdMatrix34 dir;
				SimdVector speed;
				if(windemitters)
				{
					//use 3d exact direction, use global position in 0,0,0 and do not swing there and back (like trees or grass)
					speed = world->GetLocalWindDirVec(windemitters, vecPPos, windinfo, 0.0f, dir,  WEF_EXACT|WEF_GLOBPOS|WEF_NOSWING);
				}
				else
				{
					Vector3 v3dir;
					windinfo.GetWindDir(v3dir);

					dir = SimdMatrix34(SimdVector(v3dir.x), SimdVector(v3dir.y), SimdVector(v3dir.z));
					speed = windinfo.m_fWindSpeed;
				}
				//speed must have some borders otherwise the impact is to huge
				speed = speed * SimdVector(WindPartSpeedMul);

				SimdVector mul = speed.Min(WindPartMaxSpeed) * vecTimeSlice;
				vecVel = vecVel + (dir * mul);
			}
#endif			

			vecVel.vec[2] = gravity.MultiplyAdd(SimdVector::LoadFloat4A16(partGravity), vecVel.vec[2]);

#ifdef EN_DONE
			if(eflags & PEF_COLLIDING)
			{
				SimdMatrix34 vel = vecVel * vecTimeSlice;
				SimdMatrix34 end = vecPPos + vel;
				SimdVector velSqr = vel.SqrLen();
				
				SimdVector fastEnough = velSqr > SimdVector(0.001f);
				SimdScalar cond = fastEnough.HasTrue();
				
				//is there at least one particle to trace
				if(cond.IsTrue())
				{
					//find out, which particles to trace
					ENF_ALIGN16(float) proceedMask[4];
					SimdVector condMask = fastEnough & SimdVector::One();
					condMask.StoreFloat4A16(proceedMask);

					//devectorize
					ENF_ALIGN16(float) tmpPos[12], tmpVel[12], tmpEnd[12];
					vecPPos.StoreFloat3x4A16(tmpPos);
					end.StoreFloat3x4A16(tmpEnd);
					vecVel.StoreFloat3x4A16(tmpVel);
					
					float lowerLife[4] = {0.f, 0.f, 0.f, 0.f};
					for(uint i = 0; i < validIdx; ++i)
					{
						Vector3 v3pos = Vector3(tmpPos[i], tmpPos[i+4], tmpPos[i+8]);
						if((uint)proceedMask[i])
						{
							Vector3 v3end(tmpEnd[i], tmpEnd[i+4], tmpEnd[i+8]);
							if(world->TraceMove(node, v3pos, v3end , &trace))
							{
								//reflect only part of energy
								float dotVelPlane = tmpVel[i] * trace.Plane.x + tmpVel[i+4] * trace.Plane.y + tmpVel[i+8] * trace.Plane.z; 
								float backoff = dotVelPlane * (1.0f + def->m_fRestitution);	
								tmpVel[i] -= trace.Plane.x * backoff;
								tmpVel[i+4] -= trace.Plane.y * backoff;
								tmpVel[i+8] -= trace.Plane.z * backoff;
								
								//lower lifetime
								lowerLife[i] = 1.f;
							}
							tmpPos[i] = trace.EndPos.x;
							tmpPos[i+4] = trace.EndPos.y;
							tmpPos[i+8] = trace.EndPos.z;
						}
					}
					//go back to simd form
					vecPPos = end.Select(SimdMatrix34(tmpPos), fastEnough);
					vecVel = vecVel.Select(SimdMatrix34(tmpVel), fastEnough);

					SimdVector timeMask = SimdVector(lowerLife[0], lowerLife[1], lowerLife[2], lowerLife[3]) > 0.0f;
					ntime = ntime + (timeMask & ((SimdVector::One() - ntime) * def->m_fLifetimeShortening));
				}
				else
				{
					vecPPos = end;
				}
			}
			else
#endif
			{
				vecPPos = MultiplyAdd(vecVel, vecTimeSlice, vecPPos);
			}
			
			if(dospring)
			{
				//plane on particle position
				Vector3 rmatRow2 = Vector3(rmat.GetRow(2));
				SimdVector scale = vecPPos.Dot(rmatRow2.v);
				scale = -scale;
				scale = scale + pdot;
				
				SimdMatrix34 intersect((scale * rmatRow2.x) + pos.x, (scale * rmatRow2.y) + pos.y, (scale * rmatRow2.z) + pos.z);
				vecPPos = MultiplyAdd(intersect - vecPPos, SimdVector(spring), vecPPos);
			}

			if(!(eflags & PEF_NO_AIR_RESIST))
			{
				SimdVector airRes = SimdVector::LoadFloat4A16(partAirRes);
				airRes = vecTimeSlice.MultiplyAdd(airRes, SimdVector::One());
				vecVel = vecVel * airRes.Inv();
			}
		
		//update bbox
			//doesn't respect graphs, it's not worth the effort
			SimdVector pmask = partMask[validIdx - 1];//.v;
			SimdVector curMin = PartMin(vecPPos, pmask) - radius;
			SimdVector curMax = PartMax(vecPPos, pmask) + radius;
			sphereCoordMin = sphereCoordMin.Min(curMin);
			sphereCoordMax = sphereCoordMax.Max(curMax);
			
			if(!(eflags & PEF_NO_ANGULAR_VEL))
			{
				for(uint i = 0; i < validIdx; ++i)
				{
					float angleVel = part[i]->m_fAngVel * timeslice;
					if(!(eflags & PEF_NO_ANGULAR_VEL_GRAPH))
					{
						frac[i] *= (PART_GRAPH_SIZE - 1);
						float flr = enf_floor(frac[i]);
						float gpos = (int)flr;
						frac[i] -= flr;
						
						angleVel *= def->m_pAngleVelGraph->GetValue(gpos, frac[i]);
					}
					part[i]->m_fAngle += angleVel;
				}
			}

			//get values from vectors and use them as a result
			ENF_ALIGN16(float) tmpTime[4];
			ntime.StoreFloat4A16(tmpTime);
			
			//backup values which are following Vector3. Store will change 4 elements, not just 3!
			float timeScaleBck[4], angleBck[4];
			for(uint i = 0; i < validIdx; ++i)
			{
				timeScaleBck[i] = part[i]->m_fTimeScale;
				angleBck[i] = part[i]->m_fAngle;
				//store time
				m_pParticleTimes[selItems[i]] = tmpTime[i];
			}
			

			vecPPos.StoreFloat4x4A16(part[0]->m_vPosition.v, part[1]->m_vPosition.v, part[2]->m_vPosition.v, part[3]->m_vPosition.v);
			vecVel.StoreFloat4x4A16(part[0]->m_vVelocity.v, part[1]->m_vVelocity.v, part[2]->m_vVelocity.v, part[3]->m_vVelocity.v);
			
			//restore lost values
			for(uint i = 0; i < validIdx; ++i)
			{
				part[i]->m_fTimeScale = timeScaleBck[i];
				part[i]->m_fAngle = angleBck[i];
			}
		}

		if(m_iNumActiveParticles == 0)
			m_iBottom = m_iTop = 0;

		ENF_ALIGN16(float) sphereMin[4], sphereMax[4];
			
		sphereCoordMin.StoreFloat4A16(sphereMin);
		sphereCoordMax.StoreFloat4A16(sphereMax);

		bbox.AddMin(Vector3(sphereMin));
		bbox.AddMax(Vector3(sphereMax));
	}	

	//----------------------
	//emit new particles
	//----------------------
	//total emitor lifetime 
	
	float prevtime = m_fTime;
	float time = prevtime + timeslice;

	if(time >= m_fEmitorTime)
	{
		if(!(eflags & PEF_REPEAT))
		{
			if(!(flags & PUF_FORCE_LOOP))
			{
				m_fTime = m_fEmitorTime;
				return true;
			}

			time = 0;
		}
		else
		{
			do 
			{
				time -= m_fEmitorTime;
			} while (time >= m_fEmitorTime);
		}
	}
	m_fTime = time;

	//do not interpolate wrapped time. It's completely dumb and should be avoided!
	if(prevtime > time)
		prevtime = 0;

	//position and fraction to master graphs
	float starttime = (prevtime / m_fEmitorTime) * (PART_GRAPH_SIZE - 1);
	float endtime = (time / m_fEmitorTime) * (PART_GRAPH_SIZE - 1);

	float flr = enf_floor(endtime);
	int gpos = (int)flr;
	float frac = endtime - flr;

	m_fEmitTime += timeslice;
	
	float birthRate = m_fBirthRate + RandNumber::RandFloat0X(m_fBirthRateRND);

	//affect birthrate by velocity
	if(m_fBirthRateVel != 0.0f)
	{
		float vel = UNIT2METER(mat.GetTranslation().Distance(Vector3(&ai->m_vPrevPos.v[0]))) / timeslice;
		//scale by current birthrate setting
		vel *= m_fBirthRate / def->m_fBirthRate;
		birthRate += vel * m_fBirthRateVel;
	}

	//TODO: in case of huge time-steps, we have to find maximum birthrate from last time, to now
	float rate = birthRate * (def->m_pMBirthRateGraph ? def->m_pMBirthRateGraph->GetValue(gpos, frac) : def->m_fMBirthRateConstant);

	int emit = enf_floor(rate * m_fEmitTime);

	m_fEmitTime -= (float)emit * m_fBirthRate;
	if(m_fEmitTime < 0)
		m_fEmitTime = 0;

	if(emit)
	{
		SimdMatrix34 xmat = SimdMatrix34::LoadFloat4x3((const float*)&mat);
		SimdVector xprev = SimdVector::LoadFloat4(ai->m_vPrevPos.v);
		if(eflags & PEF_OFFSET)
		{
			xprev = xprev + xmat.TransformVector(SimdVector::LoadFloat4(m_vEmitOffsets.v));
		}

/*		Vector3 prev = ai->m_vPrevPos.v;

		if(eflags & PEF_OFFSET)
			prev += mat.TransformVector(m_vEmitOffsets);
*/
		SimdVector xpos = SimdVector::LoadFloat4(pos.v);
		
		SimdVector emitShape = SimdVector::LoadFloat4A16(def->m_vEmitShape.v);
		EmitorDef::EmitorType emitType = def->m_eEmitorType;
		if(emitType == EmitorDef::SPHERE)
			emitShape = emitShape.SplatX();
		SimdVector xlerp = SimdVector::Zero();
		SimdVector xinc = 1.0f / emit;

		for(int n = 0; n < emit; n++)
		{
			if(m_iNumActiveParticles >= (m_iMaxParticles - 1))
				return false;

			Particle* part = &m_pParticles[m_iTop];
			m_pParticleTimes[m_iTop] = 0;
			float lerp = (float)n / emit;

			float mtime = enf_lerp(starttime, endtime, lerp);
			flr = enf_floor(mtime);
			gpos = (int)flr;
			frac = mtime - flr;

//			EmitNew(part, def, mat, pos, prev, lerp);
			EmitNew(part, emitType, xmat, emitShape, xpos, xprev, xlerp);
			xlerp = xlerp + xinc;

			if(eflags & PEF_LIFETIME_BY_ANIM)
				part->m_fTimeScale = 1.0f / (def->m_iTilesCount / (def->m_fAnimFPS + RandNumber::RandFloat0X(m_fLifetimeRND)));
			else
				part->m_fTimeScale = 1.0f / (m_fLifetime + RandNumber::RandFloat0X(m_fLifetimeRND));

			float scale = RandNumber::RandFloat0X(m_fVelocityRND) + m_fVelocity;
			float angle = RandNumber::RandFloat0X(m_vEmitAngle.x);
			float elev = (ENF_PI * 0.5f) + RandNumber::RandFloat0X(m_vEmitAngle.y - m_vEmitAngle.z) + m_vEmitAngle.z;
			float c = cosf(elev);
			part->m_vVelocity = rmat.TransformVector(Vector3(c * cosf(angle) * scale, c * sinf(angle) * scale, sinf(elev) * scale)) + vel;

			if(eflags & PEF_RANDOM_ANGLE)
				part->m_fAngle = RandNumber::RandFloat0X(ENF_PI2);
			else
				part->m_fAngle = 0;

			part->m_fGravityScale = m_fGravityMult + RandNumber::RandFloat0X(m_fGravityMultRND);
			part->m_fAirResistance = m_fAirResistance + RandNumber::RandFloat0X(m_fAirResistanceRND);
			float angvel = m_fAngleVelMult * (def->m_pMAngleVelGraph ? def->m_pMAngleVelGraph->GetValue(gpos, frac) : def->m_fMAngleVelConstant);

			if(eflags & PEF_RANDOM_ROTATION && RandNumber::RandIntXX(2) > 0)
				angvel = -angvel;
			part->m_fAngVel = angvel;
			part->m_fMTime = mtime * PART_INVGRAPH_SIZE;

			uint newtop = m_iTop + 1;
			newtop = (newtop == m_iMaxParticles) ? 0 : newtop;

			if(newtop == m_iBottom)
			{
				//overwrite bottom particle
				uint newbottom = newtop + 1;
				newbottom = (newbottom == m_iMaxParticles) ? 0 : newbottom;
				m_iBottom = newbottom;
				
				//wasn't the particle dead?
				if(ISDEAD(m_pParticleTimes[newtop]))
					m_iNumActiveParticles++;
			}
			else
				m_iNumActiveParticles++;

			m_iTop = newtop;
		}
		enf_assert(m_iNumActiveParticles < m_iMaxParticles);

/*#ifdef _DEBUG
		{
			uint num = 0;
			uint next = 0;
			for(uint n = m_iBottom; n != m_iTop; n = next)
			{
				if(m_pParticleTimes[n] != -1)
					num++;
				next = n + 1;
				next = (next == m_iMaxParticles) ? 0 : next;
			}
			enf_assert(num == m_iNumActiveParticles);
		}
#endif*/

	}
	return false;
}

//-----------------------------------------------------------------------------
void PEmitor::Restart(const EmitorDef* def, ParticleEffectInstance* ai)
{
	m_iTop = m_iBottom = 0;
}


//-----------------------------------------------------------------------------
const uint PART_SORT_SHIFT = 12;
const uint PART_SORT_INDEX_MASK = ((1 << PART_SORT_SHIFT) - 1);


//-----------------------------------------------------------------------------
ENF_INLINE void FillPointSprite(PSpriteVert* ENF_RESTRICT pf2, const Particle* ENF_RESTRICT part, uint p, float acttime)
{
	_ReadWriteBarrier();

	pf2->Origin[0] = part[p].m_vPosition.x;
	pf2->Origin[1] = part[p].m_vPosition.y;
	pf2->Origin[2] = part[p].m_vPosition.z;
	pf2->MTime = part[p].m_fMTime;
	pf2->Time  = acttime;

	_ReadWriteBarrier();
}

//-----------------------------------------------------------------------------
void* ParticleSystem::XRenderPS(void* ENF_RESTRICT vptr, const ParticleEffectInstance* pi, uint emitor, const BillboardSort& billsort)
{
	const EmitorDef* def = &pi->GetVObject()->m_pEmitorDefs[emitor];
	const PEmitor* ENF_RESTRICT pe = pi->m_pEmitors[emitor];

	uint act = pe->m_iNumActiveParticles;
	EngineStats::m_iParticleCount += act;

	PSpriteVert* ENF_RESTRICT pf2 = (PSpriteVert*) vptr;

	const Particle *part = pe->m_pParticles;
	const float* ptimes = pe->m_pParticleTimes;
	uint flags = pe->m_iFlags;

	if ((def->m_iFlags & PEF_SORT) && ENF_DIAG_RBOOL(DM_DISABLE_PARTSORT))
	{
		uint* sort = ALLOCA(uint, act);
		uint num = 0;

		enf_assert(def->m_iMaxParticles < ((1 <<  PART_SORT_SHIFT) - 1));

		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;

				float d = billsort.m_vSort.Dot(part[p].m_vPosition) + billsort.m_fSortZ;
				uint dist = enf_toint(d);

				sort[num++] = (dist << PART_SORT_SHIFT) | p;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;
		}

		QuickSortCmp<uint, ReverseCompareT<uint> >(num, sort);


		for (uint i = 0; i < num; i++)
		{
			uint p = sort[i] & PART_SORT_INDEX_MASK;
			FillPointSprite(pf2, part, p, ptimes[p]);
			pf2++;
		}
	} 
	else
	{
		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;
				FillPointSprite(pf2, part, p, ptimes[p]);
				pf2++;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;
		}
	}
	
	return pf2;
}



/*!
to simplify the parameters delivering
*/
struct SpriteParamStruct
{
	float animfps;
	float fanimframesM1;
	uint animframes;
	bool velangle;
	bool randframe;
	bool animonce;
};


//-----------------------------------------------------------------------------
ENF_INLINE void FillSprite(SpriteVert* ENF_RESTRICT pf2, const Particle* ENF_RESTRICT part, uint p, float acttime, const SpriteParamStruct& spritedef, const ParticleSystem::BillboardSort& billsort)
{
	const Vector3& pos = part[p].m_vPosition;

	//compute angle
	float angle;
	if (spritedef.velangle)
	{
		float x = billsort.m_vBillboard1.Dot(part[p].m_vVelocity);
		float y = billsort.m_vBillboard2.Dot(part[p].m_vVelocity);

		//very time consuming
		angle = atan2(x, -y);
	} 
	else
	{
		angle = part[p].m_fAngle;
	}

	//time and frame for possible anim
	float mtime = part[p].m_fMTime;
	if(spritedef.animframes)
	{
		float frame;
		if (spritedef.randframe)
		{
			frame = RandNumber::HashValue(p) % spritedef.animframes;
		}
		else
		{
			frame = enf_floor((acttime / part[p].m_fTimeScale) * spritedef.animfps);

			if(spritedef.animonce)
				frame = enf_min(frame, spritedef.fanimframesM1);
		}

		mtime += frame;
	}

	_ReadWriteBarrier();

	pf2->Origin[0] = pos.x;
	pf2->Origin[1] = pos.y;
	pf2->Origin[2] = pos.z;
	pf2->MTime  = mtime;
	pf2->Time	= acttime;
	pf2->Angle	= angle;

	_ReadWriteBarrier();
}



//-----------------------------------------------------------------------------
void* ParticleSystem::XRenderSprites(void* ENF_RESTRICT vptr, const ParticleEffectInstance* pi, uint emitor, const BillboardSort& billsort)
{
	const EmitorDef* def = &pi->GetVObject()->m_pEmitorDefs[emitor];
	const PEmitor* pe = pi->m_pEmitors[emitor];

	uint act = pe->m_iNumActiveParticles;
	EngineStats::m_iParticleCount += act;

	SpriteVert* ENF_RESTRICT pf2 = (SpriteVert*)vptr;
	const Particle *part = pe->m_pParticles;
	const float* ptimes = pe->m_pParticleTimes;

	SpriteParamStruct spritedef;
	spritedef.animframes = 0;
	spritedef.velangle = (def->m_iFlags & PEF_VEL_ANGLE) != 0;

	if(pe->m_iFlags & (PEF_ANIMATE | PEF_RANDOM_FRAME | PEF_ANIM_ONCE))
	{
		spritedef.animfps = def->m_fAnimFPS;
		spritedef.animframes = def->m_iTilesCount;
		spritedef.fanimframesM1 = def->m_iTilesCount - 1;
		spritedef.randframe = (pe->m_iFlags & PEF_RANDOM_FRAME) != 0;
		spritedef.animonce = (pe->m_iFlags & PEF_ANIM_ONCE) != 0;
	}

	if ((def->m_iFlags & PEF_SORT) && ENF_DIAG_RBOOL(DM_DISABLE_PARTSORT))
	{
		uint* sort = ALLOCA(uint, def->m_iMaxParticles);
		uint num = 0;

		enf_assert(def->m_iMaxParticles < ((1 <<  PART_SORT_SHIFT) - 1));

		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;

				float d = billsort.m_vSort.Dot(part[p].m_vPosition) + billsort.m_fSortZ;
				uint dist = enf_toint(d);
					
				sort[num++] = (dist << PART_SORT_SHIFT) | p;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;
		}

		QuickSortCmp<uint, ReverseCompareT<uint> >(num, sort);

		for (uint i = 0; i < num; i++)
		{
			uint p = sort[i] & PART_SORT_INDEX_MASK;
			FillSprite(pf2, part, p, ptimes[p], spritedef, billsort);
			pf2++;
		}
	} 
	else
	{
		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;
				FillSprite(pf2, part, p, ptimes[p], spritedef, billsort);
				pf2++;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;
		}
		enf_assert(act == 0);
	}

	return pf2;
}


//-----------------------------------------------------------------------------
ENF_INLINE void FillStreak(StreakVert* ENF_RESTRICT pf2, const Particle* ENF_RESTRICT part, uint p, float acttime)
{
	_ReadWriteBarrier();

	pf2->Origin[0] = part[p].m_vPosition.x;
	pf2->Origin[1] = part[p].m_vPosition.y;
	pf2->Origin[2] = part[p].m_vPosition.z;
	pf2->MTime  = part[p].m_fMTime;
	pf2->Time   = acttime;
	pf2->Velocity[0] = part[p].m_vVelocity.x;
	pf2->Velocity[1] = part[p].m_vVelocity.y;
	pf2->Velocity[2] = part[p].m_vVelocity.z;

	_ReadWriteBarrier();
}



//-----------------------------------------------------------------------------
void* ParticleSystem::XRenderStreaks(void* ENF_RESTRICT vptr, const ParticleEffectInstance* pi, uint emitor, const BillboardSort& billsort)
{
	const EmitorDef* def = &pi->GetVObject()->m_pEmitorDefs[emitor];
	const PEmitor* pe = pi->m_pEmitors[emitor];

	uint act = pe->m_iNumActiveParticles;
	EngineStats::m_iParticleCount += act;

	StreakVert* ENF_RESTRICT pf2 = (StreakVert*) vptr;
	const Particle* part = pe->m_pParticles;
	const float* ptimes = pe->m_pParticleTimes;

	if ((def->m_iFlags & PEF_SORT) && ENF_DIAG_RBOOL(DM_DISABLE_PARTSORT))
	{
		uint* sort = ALLOCA(uint, act);
		uint num = 0;

		enf_assert(def->m_iMaxParticles < ((1 <<  PART_SORT_SHIFT) - 1));

		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;

				float d = billsort.m_vSort.Dot(part[p].m_vPosition) + billsort.m_fSortZ;
				uint dist = enf_toint(d);

				sort[num++] = (dist << PART_SORT_SHIFT) | p;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;
		}

		QuickSortCmp<uint, ReverseCompareT<uint> >(num, sort);

		for (uint i = 0; i < num; i++)
		{
			uint p = sort[i] & PART_SORT_INDEX_MASK;
			FillStreak(pf2, part, p, ptimes[p]);
			pf2++;
		}
	} 
	else
	{
		for(uint p = pe->m_iBottom; act && p != pe->m_iTop;)
		{
			if((((uint*)ptimes)[p] & 0x80000000) == 0) //avoid float compare
			{
				act--;
				FillStreak(pf2, part, p, ptimes[p]);
				pf2++;
			}

			if(++p >= pe->m_iMaxParticles)
				p = 0;		
		}
	}

	return pf2;
}


//-----------------------------------------------------------------------------
void ParticleSystem::AddEffectToRender(Pipeline* pipeline, Entity* ent, int distance)
{
	ParticleEffectInstance* pe = (ParticleEffectInstance*)ent->GetInstance();
	ParticleEffect *eff = (ParticleEffect*)pe->GetVObject();

	uint matSortVersion = GetMaterialSystem()->GetSortVersion();

	//update emitor sort ID's when materials order changed
	if(matSortVersion != eff->m_iSortVersion)
	{
		eff->m_iSortVersion = matSortVersion;

		enf_assert2(eff->m_iNumEmitorDefs < 16, "Too much emitters per particle effect");
		for(uint n = 0; n < eff->m_iNumEmitorDefs; n++)
		{
			eff->m_pEmitorDefs[n].m_iSortID =  ((15 - n) << 15) | 	eff->m_pEmitorDefs[n].GetIndex();
		}
	}

	//Sort value is :
	// 0 ..  14 - unique id from texgraph data
	// 15 .. 18 - num of emitter in effect
	// 19 .. 30 - quantized distance

	const float invq = 1.0f / TRANS_SORT_GRANULARITY;
	AAB wbbox;
	wbbox.InitFromBox(ent->IGetTranslation(), pe->m_BoundingBox);

	Vector3 obj = wbbox.GetNearestPosition(pipeline->GetCameraMatrix().GetTranslation()) * invq;

	//quantize
	obj.x = enf_trunc(obj.x);
	obj.y = enf_trunc(obj.y);
	obj.z = enf_trunc(obj.z);

	Vector3 distVector = obj - pipeline->GetQuantizedCameraPos();
	const Vector3& camDir = pipeline->GetCameraMatrix().Direction();//GetRow(0);
	distance = enf_toint(Vector3(camDir.x, camDir.y, -camDir.z).Dot(distVector));

	//need to have two highest bits free -> the highest means the sign and the second is reserve, because in 
	//BeginMesh the unique material id is added and the value can overlap to higher bit
	int idistance = TRANS_MAX_QUANT_DISTANCE - enf_min(distance , TRANS_MAX_QUANT_DISTANCE);

	for(uint n = 0; n < eff->m_iNumEmitorDefs; n++)
	{
		if(pe->m_pEmitors[n]->m_iNumActiveParticles == 0)
			continue;

		//does it have to be LODed?
		if(eff->m_pEmitorDefs[n].m_iLOD)
		{
			if(distance > ParticleSystem::m_iLODDistances[eff->m_pEmitorDefs[n].m_iLOD - 1])
				continue;
		}
#ifdef _DEBUG
		pe->m_iLocked++;
#endif
		EmitorItem& eitem = m_pEffectsToRender.Append();
		eitem.m_iSortValue = (idistance << 19) | eff->m_pEmitorDefs[n].m_iSortID;
		eitem.m_iEmitor = n;
		eitem.m_pEntity = ent;
	}
}

//---------------------------------------------------------------------------
void enf_swap(ParticleSystem::EmitorItem& a, ParticleSystem::EmitorItem& b)
{
	enf_swap(a.m_iSortValue, b.m_iSortValue);
	enf_swap(a.m_iEmitor, b.m_iEmitor);
	enf_swap(a.m_pEntity, b.m_pEntity);
}


//---------------------------------------------------------------------------
void ParticleEffect::Init()
{
	VObject::Init();
	m_pEmitorDefs = NULL;	
	m_iNumEmitorDefs = 0;
	m_fCRadius = m_fRadius = 256.0f;
	m_iSortVersion = -1;
	m_iAllFlags = 0;
	m_fVelocityAffection = 0;
}

//---------------------------------------------------------------------------
void ParticleEffect::Unload()
{
	VObject::Unload();
	
	IL_EXCHANGE_ADD(MemStats::m_iParticles, GetSize());

	for(uint n = 0; m_pEmitorDefs != 0 && n < m_iNumEmitorDefs; n++)
	{
		if(m_pEmitorDefs[n].m_pMaterial)
		{
			m_pEmitorDefs[n].m_pMaterial->Unload();
			m_pEmitorDefs[n].m_pMaterial->Release();
		}
	}

	SAFE_DELETE_ARRAY(m_pEmitorDefs);
}

//---------------------------------------------------------------------------
size_t ParticleEffect::GetSize() const
{
	size_t size = sizeof(EmitorDef) * m_iNumEmitorDefs;
	for(uint n = 0; n < m_iNumEmitorDefs; n++)
	{
		if(m_pEmitorDefs[n].m_pMBirthRateGraph)
			size += sizeof(EmitorDef::FloatGraph);
		if(m_pEmitorDefs[n].m_pMAngleVelGraph)
			size += sizeof(EmitorDef::FloatGraph);
		if(m_pEmitorDefs[n].m_pAngleVelGraph)
			size += sizeof(EmitorDef::FloatGraph);
	}

	return size;
}

//---------------------------------------------------------------------------
VObjectInstance* ParticleEffect::AllocInstance(Entity* ent, CStr options)
{
	ParticleEffectInstance* pa = ENF_NEW ParticleEffectInstance(this, ent);

	if(m_iAllFlags & PEF_WIND)
		pa->SetFlags(VIF_WINDRECEIVER);

	return pa;
}

//-----------------------------------------------------------------------
const char* ParticleEffect::EmitorName(uint emitor) const
{
	enf_assert(emitor < m_iNumEmitorDefs);
	return m_pEmitorDefs[emitor].m_strName.cstr();
}

//-----------------------------------------------------------------------
const PEmitor* ParticleEffectInstance::GetEmitor(uint emitor) const
{
	enf_assert(emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs);
	return m_pEmitors[emitor];
}

//-----------------------------------------------------------------------
PEmitor* ParticleEffectInstance::GetEmitor(uint emitor)
{
	enf_assert(emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs);
	return m_pEmitors[emitor];
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetBirthRate(uint emitor, float value)
{
	if(emitor != INDEX_NOT_FOUND)
		GetEmitor(emitor)->m_fBirthRate = value;
	else
	{
		for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
		{
			GetEmitor(emitor)->m_fBirthRate = value;
		}
	}
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetRepeat(uint emitor, bool repeat)
{
	if(repeat)
	{
		if(emitor != INDEX_NOT_FOUND)
			GetEmitor(emitor)->m_iFlags |= PEF_REPEAT;
		else
		{
			for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
			{
				GetEmitor(emitor)->m_iFlags |= PEF_REPEAT;
			}
		}
	}
	else
	{
		if(emitor != INDEX_NOT_FOUND)
			GetEmitor(emitor)->m_iFlags &= ~PEF_REPEAT;
		else
		{
			for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
			{
				GetEmitor(emitor)->m_iFlags &= ~PEF_REPEAT;
			}
		}
	}
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetBirthRateRND(uint emitor, float value)
{
	if(emitor != INDEX_NOT_FOUND)
		GetEmitor(emitor)->m_fBirthRateRND = value;
	else
	{
		for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
		{
			GetEmitor(emitor)->m_fBirthRateRND = value;
		}
	}
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetSize(uint emitor, float value)
{
	if(emitor != INDEX_NOT_FOUND)
		GetEmitor(emitor)->m_fSize = value;
	else
	{
		for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
		{
			GetEmitor(emitor)->m_fSize = value;
		}
	}

	//disable batching
	GetEmitor(emitor)->DisableBatching();
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetVelocity(uint emitor, float value)
{
	if(emitor != INDEX_NOT_FOUND)
		GetEmitor(emitor)->m_fVelocity = value;
	else
	{
		for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
		{
			GetEmitor(emitor)->m_fVelocity = value;
		}
	}
}

//-----------------------------------------------------------------------
void ParticleEffectInstance::SetLifetime(uint emitor, float value)
{
	if(emitor != INDEX_NOT_FOUND)
		GetEmitor(emitor)->m_fLifetime = value;
	else
	{
		for(emitor = 0; emitor < ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs; emitor++)
		{
			GetEmitor(emitor)->m_fLifetime = value;
		}
	}
}

//-----------------------------------------------------------------------
const Vector3& ParticleEffectInstance::GetConeAngle(uint emitor) const
{
	return GetEmitor(emitor)->m_vEmitAngle;
}

//-----------------------------------------------------------------------
const Vector3& ParticleEffectInstance::GetEmitOffset(uint emitor) const
{
	return GetEmitor(emitor)->m_vEmitOffsets;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetVelocity(uint emitor) const
{
	return GetEmitor(emitor)->m_fVelocity;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetAngularVelocity(uint emitor) const
{
	return GetEmitor(emitor)->m_fAngleVelMult;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetSize(uint emitor) const
{
	return GetEmitor(emitor)->m_fSize;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetStretch(uint emitor) const
{
	return GetEmitor(emitor)->m_fStretch;
}

//-----------------------------------------------------------------------
bool ParticleEffectInstance::GetRandomAngle(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_RANDOM_ANGLE) != 0);
}

//-----------------------------------------------------------------------
bool ParticleEffectInstance::GetRandomRotation(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_RANDOM_ROTATION) != 0);
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetAirResistance(uint emitor) const
{
	return GetEmitor(emitor)->m_fAirResistance;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetAirResistanceRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fAirResistanceRND;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetGravityScale(uint emitor) const
{
	return GetEmitor(emitor)->m_fGravityMult;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetGravityScaleRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fGravityMultRND;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetBirthRate(uint emitor) const
{
	return GetEmitor(emitor)->m_fBirthRate;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetBirthRateRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fBirthRateRND;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetLifetime(uint emitor) const
{
	return GetEmitor(emitor)->m_fLifetime;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetLifetimeRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fLifetimeRND;
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetEffectTime(uint emitor) const
{
	return GetEmitor(emitor)->m_fEmitorTime;
}

//-----------------------------------------------------------------------
bool ParticleEffectInstance::GetRepeat(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_REPEAT) != 0);
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetElapsedTime(uint emitor) const
{
	return GetEmitor(emitor)->m_fTime;
}

//-----------------------------------------------------------------------
uint ParticleEffectInstance::GetActiveParticles(uint emitor) const
{
	return GetEmitor(emitor)->m_iNumActiveParticles;
}

//-----------------------------------------------------------------------
bool ParticleEffectInstance::GetSort(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_SORT) != 0);
}

//-----------------------------------------------------------------------
bool ParticleEffectInstance::GetWind(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_WIND) != 0);
}

//-----------------------------------------------------------------------
float ParticleEffectInstance::GetSpring(uint emitor) const
{
	return GetEmitor(emitor)->m_fSpring;
}

//-----------------------------------------------------------------------
AAB ParticleEffectInstance::ComputeWorldBoundBox(const Matrix43& mat, uint flags) const
{
	AAB box;

	//we already have world boundbox
	box.InitFromBox(mat.GetTranslation(), m_BoundingBox);
	enf_assert(box.IsValid());

	return box;
}

//---------------------------------------------------------------------------
uint ParticleEffectInstance::GetTotalActiveParticles() const
{
	const ParticleEffect* pe = (ParticleEffect*)m_pObject;

	uint count = 0;
	for(uint n = 0; n < pe->m_iNumEmitorDefs; n++)
	{
		count += m_pEmitors[n]->m_iNumActiveParticles;
	}

	return count;
}
//---------------------------------------------------------------------------
bool ParticleEffectInstance::IsLooped() const
{
	uint numEmitors = ((const ParticleEffect*)m_pObject)->m_iNumEmitorDefs;

	uint count = 0;
	for(uint n = 0; n < numEmitors; n++)
	{
		if((m_pEmitors[n]->m_iFlags & PEF_REPEAT) != 0)
			return true;
	}

	return false;
}

//---------------------------------------------------------------------------
bool ParticleEffectInstance::HasActiveParticles() const
{
	uint numEmitors = ((const ParticleEffect*)m_pObject)->m_iNumEmitorDefs;

	uint count = 0;
	for(uint n = 0; n < numEmitors; n++)
	{
		if(m_pEmitors[n]->m_iNumActiveParticles)
			return true;
	}

	return false;
}

//---------------------------------------------------------------------------
bool ParticleEffectInstance::Init()
{
	ParticleEffect* pe = (ParticleEffect*)m_pObject;

	m_pEmitors = ENF_NEW PEmitor*[pe->m_iNumEmitorDefs];
	ZeroMem(m_pEmitors, pe->m_iNumEmitorDefs * sizeof(PEmitor*));
	MemStats::m_iParticles += sizeof(*this);

	//
	m_BoundingBox.Init(-4, -4, -4, 4, 4, 4);

	for(uint n = 0; n < pe->m_iNumEmitorDefs; n++)
	{
		m_pEmitors[n] = pe->m_pEmitorDefs[n].CreateEmitor(true);
	}

	SetFlags(VIF_NEEDS_AMBIENT_LIGHT);
	m_fVelocityAffection = pe->m_fVelocityAffection;
#ifdef _DEBUG
	m_iLocked = 0;
#endif

	return true;
}

//---------------------------------------------------------------------------
ParticleEffectInstance::ParticleEffectInstance(VObject* obj, Entity *owner): VObjectInstance(obj, owner), m_vPrevPos(owner->IGetTranslation()), m_bFirstUpdate(false), m_eState(SS_IDLE), m_vVelocity(0.0f), m_fVelocityAffection(0.0f), m_iLastResult(0)
{
	Init();
}

//---------------------------------------------------------------------------
void ParticleEffectInstance::Unload()
{
	ParticleEffect* pe = (ParticleEffect*)m_pObject;

	for(uint n = 0; n < pe->m_iNumEmitorDefs; n++)
	{
		pe->m_pEmitorDefs[n].DestroyEmitor(m_pEmitors[n]);
	}

	SAFE_DELETE_ARRAY(m_pEmitors);	
	MemStats::m_iParticles -= sizeof(*this);
}

//---------------------------------------------------------------------------
ParticleEffectInstance::~ParticleEffectInstance()
{
	enf_assert(IL_READ(m_eState) != SS_SUBMITED);

#ifdef _DEBUG
	enf_assert2(m_iLocked == 0, "Trying to delete ParticleEffectInstance when it's rendered");
#endif
	Unload();
}

//---------------------------------------------------------------------------
void ParticleEffectInstance::Serialize(SerializeInputAdapter* adapter)
{
	enf_assert(GetTypeName() == "ParticleEffectInstance");

	VObjectInstance::Serialize(adapter);

	ParticleEffect* pe = (ParticleEffect*)m_pObject;

	m_vPrevPos = adapter->ReadVector4();
	m_vVelocity = adapter->ReadVector3();
	m_fVelocityAffection = adapter->ReadFloat();
	m_BoundingBox = adapter->ReadAAB();

	uint numemitors = pe->m_iNumEmitorDefs;

	for(uint n = 0; n < numemitors; n++)
	{
		PEmitor* emitor = m_pEmitors[n];

		uint maxpart = adapter->ReadUInt32();

		ZeroMem(emitor->m_pParticles, sizeof(Particle) * maxpart);

		emitor->m_iTop = adapter->ReadUInt32();
		emitor->m_iBottom = adapter->ReadUInt32();
		emitor->m_fEmitTime = adapter->ReadFloat();
		emitor->m_fTime = adapter->ReadFloat();
		emitor->m_fVelocity = adapter->ReadFloat();
		emitor->m_fVelocityRND = adapter->ReadFloat();
		emitor->m_vEmitOffsets = adapter->ReadVector3();
		emitor->m_vEmitAngle = adapter->ReadVector3();
		emitor->m_fGravityMult = adapter->ReadFloat();
		emitor->m_fGravityMultRND = adapter->ReadFloat();
		emitor->m_fSize = adapter->ReadFloat();
		emitor->m_fStretch = adapter->ReadFloat();
		emitor->m_fLifetime = adapter->ReadFloat();
		emitor->m_fLifetimeRND = adapter->ReadFloat();
		emitor->m_fBirthRate = adapter->ReadFloat();
		emitor->m_fBirthRateRND = adapter->ReadFloat();
		emitor->m_fAngleVelMult = adapter->ReadFloat();
		emitor->m_fAirResistance = adapter->ReadFloat();
		emitor->m_fAirResistanceRND = adapter->ReadFloat();
		emitor->m_fEmitorTime = adapter->ReadFloat();
		emitor->m_fSpring = adapter->ReadFloat();
		emitor->m_iFlags = adapter->ReadUInt32();

		uint numActive = 0;
		for(uint p = emitor->m_iBottom; p != emitor->m_iTop; )
		{
			emitor->m_pParticleTimes[p] = adapter->ReadFloat();
			if(!ISDEAD(emitor->m_pParticleTimes[p]))
			{
				numActive++;
				emitor->m_pParticles[p].m_vPosition = adapter->ReadVector3();
				emitor->m_pParticles[p].m_vVelocity = adapter->ReadVector3();
				emitor->m_pParticles[p].m_fAngle = adapter->ReadFloat();
				emitor->m_pParticles[p].m_fTimeScale = adapter->ReadFloat();
				emitor->m_pParticles[p].m_fGravityScale = adapter->ReadFloat();
				emitor->m_pParticles[p].m_fAirResistance = adapter->ReadFloat();
				emitor->m_pParticles[p].m_fAngVel = adapter->ReadFloat();
				emitor->m_pParticles[p].m_fMTime = adapter->ReadFloat();
			}
			p++;
			p = (p == emitor->m_iMaxParticles) ? 0 : p;
		}

		emitor->m_iNumActiveParticles = numActive;
	}
}

//---------------------------------------------------------------------------
void ParticleEffectInstance::Serialize(SerializeOutputAdapter* adapter)
{
	VObjectInstance::Serialize(adapter);

	ParticleEffect* pe = (ParticleEffect*)GetVObject();

	adapter->WriteVector4(m_vPrevPos);
	adapter->WriteVector3(m_vVelocity);
	adapter->WriteFloat(m_fVelocityAffection);
	adapter->WriteAAB(m_BoundingBox);

	for(uint n = 0; n < pe->m_iNumEmitorDefs; n++)
	{
		PEmitor* emitor = m_pEmitors[n];

		enf_assert(emitor->m_iNumActiveParticles <= emitor->m_iMaxParticles);
		adapter->WriteUInt32(emitor->m_iMaxParticles);
		adapter->WriteUInt32(emitor->m_iTop); 
		adapter->WriteUInt32(emitor->m_iBottom);	
		adapter->WriteFloat(emitor->m_fEmitTime);
		adapter->WriteFloat(emitor->m_fTime);
		adapter->WriteFloat(emitor->m_fVelocity);
		adapter->WriteFloat(emitor->m_fVelocityRND);
		adapter->WriteVector3(emitor->m_vEmitOffsets);
		adapter->WriteVector3(emitor->m_vEmitAngle);
		adapter->WriteFloat(emitor->m_fGravityMult);
		adapter->WriteFloat(emitor->m_fGravityMultRND);
		adapter->WriteFloat(emitor->m_fSize);
		adapter->WriteFloat(emitor->m_fStretch);
		adapter->WriteFloat(emitor->m_fLifetime);
		adapter->WriteFloat(emitor->m_fLifetimeRND);
		adapter->WriteFloat(emitor->m_fBirthRate);
		adapter->WriteFloat(emitor->m_fBirthRateRND);
		adapter->WriteFloat(emitor->m_fAngleVelMult);
		adapter->WriteFloat(emitor->m_fAirResistance);
		adapter->WriteFloat(emitor->m_fAirResistanceRND);
		adapter->WriteFloat(emitor->m_fEmitorTime);
		adapter->WriteFloat(emitor->m_fSpring);
		adapter->WriteUInt32(emitor->m_iFlags);

		uint numActive = 0;
		for(uint p = emitor->m_iBottom; p != emitor->m_iTop; )
		{
			adapter->WriteFloat(emitor->m_pParticleTimes[p]);
			if(!ISDEAD(emitor->m_pParticleTimes[p]))
			{
				numActive++;
				adapter->WriteVector3(emitor->m_pParticles[p].m_vPosition);
				adapter->WriteVector3(emitor->m_pParticles[p].m_vVelocity);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fAngle);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fTimeScale);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fGravityScale);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fAirResistance);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fAngVel);
				adapter->WriteFloat(emitor->m_pParticles[p].m_fMTime);
			}
			p++;
			p = (p == emitor->m_iMaxParticles) ? 0 : p;
		}
		numActive=numActive;
	}
}

//---------------------------------------------------------------------------
void ParticleEffectInstance::ResetPosition(Entity* ent)
{
	m_vPrevPos = ent->IGetTranslation();
}

//---------------------------------------------------------------------------
void ParticleEffectInstance::Destroy()
{
	if(IL_COMPARE_EXCHANGE(m_eState, SS_DELETED, SS_SUBMITED) == SS_IDLE)
		delete this;
}

//---------------------------------------------------------------------------
class ParticleEffectUpdateJob: public CThreadJob
{
protected:
	BaseWorld* m_pWorld;
	Entity* m_pEntity;
	ParticleEffectInstance*	m_pInstance;
	float	m_fTimeSlice;
	uint	m_iFlags;
	Matrix43 m_Matrix;
	Vector3 m_vVelocity;
	AAB m_BBox;

public:
	ENF_DECLARE_SMALLOBJECT(ParticleEffectUpdateJob);

	ParticleEffectUpdateJob(BaseWorld* world, Entity* entity, ParticleEffectInstance* instance, float timeSlice, uint flags, const Matrix43& mat, const Vector3& vel):
		m_pWorld(world), m_pEntity(entity), m_pInstance(instance), m_fTimeSlice(timeSlice), m_iFlags(flags), m_Matrix(mat), m_vVelocity(vel)
	{
		m_BBox = m_pEntity->GetBBox();
	}
	int Run()
	{
		m_pInstance->m_iLastResult = m_pInstance->GetVObject()->InternalUpdate(m_pWorld, m_pEntity, m_pInstance, m_fTimeSlice, m_iFlags, m_Matrix, m_BBox, m_vVelocity);
		return 0;
	}
};


//---------------------------------------------------------------------------
int ParticleEffect::InternalUpdate(BaseWorld* world, Entity* ent, ParticleEffectInstance* pa, float time, uint flags, const Matrix43& mat, const AAB& entBbox, const Vector3& velocity)
{
#ifdef ENF_DONE
	LocalWindEmitter localWindEmitters[BaseWorld::MAX_ACTIVE_WIND_EMITTERS];
	uint numEmitters = 0;
	
	if(pa->IsFlagSet(VIF_WINDRECEIVER) && ENF_DIAG_RBOOL(DM_DISABLE_WIND))
	{
		uint mask = world->GetActiveWindEmitters(entBbox);

		//create local info
		for(uint n = 0; n < BaseWorld::MAX_ACTIVE_WIND_EMITTERS && mask; n++)
		{
			if(mask & (1 << n))
			{
				const WindEmitterData& wdata = world->GetWindEmitter(n);
				localWindEmitters[numEmitters].pos = wdata.m_vPos;
				localWindEmitters[numEmitters].sqRadius = wdata.m_fSqrRadius;
				localWindEmitters[numEmitters].index = n;
				numEmitters++;
				mask &= ~(1 << n);
			}
		}
	}
#else	
	LocalWindEmitter localWindEmitters[1];
	uint numEmitters = 0;
#endif

	bool ended = true;

	MTrace trace;
	_WNode* node = NULL;

#ifdef ENF_DONE
	if(m_iAllFlags & PEF_COLLIDING)
	{
		trace.Flags = TRACE_WORLD|TRACE_LINE|TRACE_ENTS;
		trace.Exclude = ent;
		trace.Mask = TFL_SOLID;
		AAB lbbox;
		lbbox.InitFromBox(Vector3().Zero(), entBbox, 16.0f);
		node = world->FindBBoxTopNode(NULL, lbbox);
	}
#endif
	uint subIter = 1;
	if(ENF_DIAG_VALUE(DM_REND_PARTICLE_HISPEED))
	{
		subIter = 10;
		time *= 0.1f;
	}

	AAB bbox(true);

	for(uint i = 0; i < subIter; i++)
	{
		for(uint n = 0; n < m_iNumEmitorDefs; n++)
		{
			ended &= pa->m_pEmitors[n]->Update(world, &m_pEmitorDefs[n], pa, mat, time, bbox, flags, localWindEmitters, numEmitters, velocity, trace, node);
		}
	}

	if(bbox.IsValid())
	{
		bbox.Translate(-mat.GetTranslation());
		bbox.Add(AAB(-4, -4, -4, 4, 4, 4));
		pa->m_BoundingBox = bbox;
	}
	else
	{
		pa->m_BoundingBox.Init(-4, -4, -4, 4, 4, 4);
	}

	pa->m_vPrevPos = mat.GetTranslation();

	return ended;
}

//---------------------------------------------------------------------------
int ParticleEffect::UpdateAnim(Entity* ent, VObjectInstance* vi, float time, uint flags, AnimMovement* movement)
{
	ENF_PROFILE(PROFILE_UPDATEPARTICLES);

	ParticleEffectInstance *pa = (ParticleEffectInstance*)vi;

	//first update
	if(time == 0)
		return 0;

	SimState ss = (SimState)IL_READ(pa->m_eState);
	if(ss != SS_IDLE)
		return 0;

	BaseWorld* world = ent->GetWorld();
	Matrix43 mat = ent->IGetNormalizedMatrix();

	//I gave up! They'll NEVER use ParticleEffect right! :-(
	if(!pa->m_bFirstUpdate)
	{
		pa->m_bFirstUpdate = true;
		pa->m_vPrevPos = mat.GetTranslation();
	}
	else if(flags & PUF_NONINTERPOLATE_MOVEMENT)
	{
		pa->m_vPrevPos = mat.GetTranslation();
	}

	Vector3 velocity(0.0f);

	if(pa->m_fVelocityAffection > 0)
	{
		velocity = pa->m_vVelocity;
		Entity* parent = ent->GetParent();
		if(parent)
		{
			PhysicsController* pc = parent->GetPhysics();
			if(pc)
			{
				velocity += pc->GetVelocityAt(ent->IGetTranslation());
			}
		}

		velocity *= pa->m_fVelocityAffection;
	}
	enf_assert(g_pParticleSystem->m_pInSimulation == NULL || g_pParticleSystem->m_pInSimulation == ent->GetWorld());

#ifdef ENF_MT_PARTICLE
	//Because Bullet is not reentrant, we cannot paralelize :-(
	if(ENF_DIAG_RBOOL(DM_REND_MT) && g_pParticleSystem->m_pInSimulation && ((m_iAllFlags & PEF_COLLIDING) == 0))
	{
		if(IL_READ(pa->m_eState) == SS_SUBMITED)
		{
			//WTF??? Already submited
			pa=pa;
		}
		else
		{
			IL_EXCHANGE(pa->m_eState, SS_SUBMITED);
			g_pParticleSystem->m_EffectsToSimulate.Insert(MakePair(pa,ent));
			GetEngine()->GetThreadManager(1)->Submit(
				ENF_NEW ParticleEffectUpdateJob(world, ent, pa, time, flags, mat, velocity)
				);
		}
	}
	else
#endif
	{
		pa->m_iLastResult = InternalUpdate(world, ent, pa, time, flags, mat, ent->GetBBox(), velocity);
		
#ifdef ENF_DONE		
		ent->SetFlags(TFL_UPDATE);
#endif
	}
	return pa->m_iLastResult;
}


//---------------------------------------------------------------------------
bool ParticleEffect::Render(const TVisibleEntity& ent, Pipeline* pipeline)
{
	if(g_pParticleSystem)
		g_pParticleSystem->AddEffectToRender(pipeline, ent.EntityPtr, ent.Distance);
	return true;
}

//-----------------------------------------------------------------------
EmitorDef* ParticleEffect::GetEmitor(uint emitor)
{
	enf_assert(emitor < m_iNumEmitorDefs);
	return &m_pEmitorDefs[emitor];
}

//-----------------------------------------------------------------------
const EmitorDef* ParticleEffect::GetEmitor(uint emitor) const
{
	enf_assert(emitor < m_iNumEmitorDefs);
	return &m_pEmitorDefs[emitor];
}

//-----------------------------------------------------------------------
const Vector3& ParticleEffect::GetConeAngle(uint emitor) const
{
	return GetEmitor(emitor)->m_vEmitAngle;
}

//-----------------------------------------------------------------------
const Vector3& ParticleEffect::GetEmitOffset(uint emitor) const
{
	return GetEmitor(emitor)->m_vEmitOffsets;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetVelocity(uint emitor) const
{
	return GetEmitor(emitor)->m_fVelocity;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetAngularVelocity(uint emitor) const
{
	return GetEmitor(emitor)->m_fAngleVelMult;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetSize(uint emitor) const
{
	return GetEmitor(emitor)->m_fSize;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetStretch(uint emitor) const
{
	return GetEmitor(emitor)->m_fStretch;
}

//-----------------------------------------------------------------------
bool ParticleEffect::GetRandomAngle(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_RANDOM_ANGLE) != 0);
}

//-----------------------------------------------------------------------
bool ParticleEffect::GetRandomRotation(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_RANDOM_ROTATION) != 0);
}

//-----------------------------------------------------------------------
float ParticleEffect::GetAirResistance(uint emitor) const
{
	return GetEmitor(emitor)->m_fAirResistance;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetAirResistanceRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fAirResistanceRND;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetGravityScale(uint emitor) const
{
	return GetEmitor(emitor)->m_fGravityMult;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetGravityScaleRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fGravityMultRND;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetBirthRate(uint emitor) const
{
	return GetEmitor(emitor)->m_fBirthRate;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetBirthRateRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fBirthRateRND;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetLifetime(uint emitor) const
{
	return GetEmitor(emitor)->m_fLifetime;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetLifetimeRand(uint emitor) const
{
	return GetEmitor(emitor)->m_fLifetimeRND;
}

//-----------------------------------------------------------------------
float ParticleEffect::GetEffectTime(uint emitor) const
{
	return GetEmitor(emitor)->m_fEmitorTime;
}

//-----------------------------------------------------------------------
bool ParticleEffect::GetRepeat(uint emitor) const
{
	return ((GetEmitor(emitor)->m_iFlags & PEF_REPEAT) != 0);
}

//-----------------------------------------------------------------------
float ParticleEffect::GetSpring(uint emitor) const
{
	return GetEmitor(emitor)->m_fSpring;
}

//---------------------------------------------------------------------------
bool ParseFloat(CParser& tp, float& res)
{
	if(!tp.NextToken(true))
		return false;

	if(!tp.AssertType(TT_QUOTE))
		return false;

	res = atof(tp.CToken->Text);
	return true;
}

//---------------------------------------------------------------------------
bool ParseFloatNQ(CParser& tp, float& res, bool skip = true)
{
	if(skip)
	{
		if(!tp.NextToken(true))
			return false;
	}

	if(!tp.AssertType(TT_NUMBER))
		return false;

	res = atof(tp.CToken->Text);
	return true;
}

//---------------------------------------------------------------------------
bool ParseIntNQ(CParser& tp, uint& res)
{
	if(!tp.NextToken(true))
		return false;

	if(!tp.AssertType(TT_NUMBER))
		return false;

	res = atol(tp.CToken->Text);
	return true;
}

//---------------------------------------------------------------------------
bool ParseVector(CParser& tp, Vector3& res)
{
	if(!tp.NextToken(true))
		return false;

	if(!tp.AssertType(TT_QUOTE))
		return false;

	double v0, v1, v2;
	if(sscanf(tp.CToken->Text, "%lf %lf %lf", &v0, &v1, &v2) == 3)
	{
		res.v[0] = (float)v0;
		res.v[1] = (float)v1;
		res.v[2] = (float)v2;
		return true;
	}

	return false;
}

//---------------------------------------------------------------------------
bool ParseInt(CParser& tp, uint& res)
{
	if(!tp.NextToken(true))
		return false;

	if(!tp.AssertType(TT_QUOTE))
		return false;

	res = atoi(tp.CToken->Text);
	return true;
}

//---------------------------------------------------------------------------
bool ParseFlag(CParser& tp, uint& res, uint flag)
{
	if(!tp.NextToken(true))
		return false;

	if(!tp.AssertType(TT_QUOTE))
		return false;

	if(atoi(tp.CToken->Text) != 0)
		res |= flag;

	return true;
}

//-------------------------------------------------------------------------------
float catmull_rom(float t, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4)
{
	float t2 = t * t;
	float t3 = t2 * t;
  
	return ( 0.5
		* ( ( - y1 + 3 * y2 -3 * y3 + y4 ) * t3
		+ ( 2 * y1 -5 * y2 + 4 * y3 - y4 ) * t2
		+ ( -y1 + y3 ) * t
		+ 2 * y2 ) );
}

//---------------------------------------------------------------------------
float GetCurvedValue(const TSrcGraph& srcGraph, float frac)
{
	uint count = srcGraph.GetCardinality();
	if(count == 0)
		return 0.0f;

	if(count == 2)
		return (srcGraph[1].Data - srcGraph[0].Data) * frac + srcGraph[0].Data;

	if(count == 1 || frac <= 0.0f)
		return srcGraph[0].Data;

	if(frac >= 1.0f)
		return srcGraph[count - 1].Data;

	for(uint n = 0; n < count; n++)
	{
		float x1 = srcGraph[n].Key;
		if(x1 <= frac && frac <= ((n < (count - 1)) ? srcGraph[n + 1].Key : 1.0f))
		{
			float y1 = srcGraph[n].Data;

			float x2 = srcGraph[n + 1].Key;
			float y2 = srcGraph[n + 1].Data;

			float x0, x3;
			float y0, y3;

			if(n > 0)
			{
				x0 = srcGraph[n - 1].Key;
				y0 = srcGraph[n - 1].Data;
			}
			else
			{
				x0 = x1 - (x2 - x1);
				y0 = y1 - (y2 - y1);
			}

			if(n < (count - 2))
			{
				x3 = srcGraph[n + 2].Key;
				y3 = srcGraph[n + 2].Data;
			}
			else
			{
				x3 = x2 + (x2 - x1);
				y3 = y2 + (y2 - y1);
			}

			float dist = x2 - x1;
			if(dist == 0.0f)
				dist = 1;

			return catmull_rom((frac - x1) / dist, x0, y0, x1, y1, x2, y2, x3, y3);
		}
	}

	//should not happen
	return 0.0f;
}

//---------------------------------------------------------------------------
void FillGraph(float *dest, uint stride, const TSrcGraph& srcGraph)
{
	for(uint n = 0; n < PART_GRAPH_SIZE; n++)
	{
		float value = GetCurvedValue(srcGraph, (float)n / (float)PART_GRAPH_SIZE);

		value = enf_min(value, 1.0f);
		value = enf_max(value, 0.0f);
		
		dest[n * stride] = value;
	}
}

//---------------------------------------------------------------------------
bool ParticleEffectInstance::SetParm(uint n, EmitorParm type, IVariable *value)
{
	uint to;

	if(n == (uint)-1)
	{
		n = 0;
		to = ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs;
	}
	else
	{
		if(n >= ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs)
			return false;

		to = n + 1;
	}

	for(; n < to; n++)
	{
		PEmitor *pe = m_pEmitors[n];

		switch(type)
		{
		case EP_CONEANGLE:
			{
				const float* ptr = (const float*)value->Pointer;
				pe->m_vEmitAngle.x = DEG2RAD(ptr[0]);
				pe->m_vEmitAngle.y = DEG2RAD(ptr[1]);
				pe->m_vEmitAngle.z = DEG2RAD(ptr[2]);
			}
			break;

		case EP_EMITOFFSET:
			pe->m_vEmitOffsets = *(Vector3*)value->Pointer;
			
			if(pe->m_vEmitOffsets.IsZero())
				pe->m_iFlags &= ~PEF_OFFSET;
			else
				pe->m_iFlags |= PEF_OFFSET;
			break;

		case EP_VELOCITY:
			pe->m_fVelocity = value->Float;
			break;

		case EP_VELOCITY_RND:
			pe->m_fVelocityRND = value->Float;
			break;

		case EP_AVELOCITY:
			pe->m_fAngleVelMult = DEG2RAD(value->Float);
			break;

		case EP_RANDOM_ROT:
			if(value->Int)
				pe->m_iFlags |= PEF_RANDOM_ROTATION;
			else
				pe->m_iFlags &= ~PEF_RANDOM_ROTATION;
			break;

		case EP_RANDOM_ANGLE:
			if(value->Int)
				pe->m_iFlags |= PEF_RANDOM_ANGLE;
			else
				pe->m_iFlags &= ~PEF_RANDOM_ANGLE;
			break;

		case EP_SIZE:
			pe->m_fSize = value->Float;
			break;

		case EP_STRETCH:
			pe->m_fStretch = value->Float;
			break;

		case EP_AIR_RESISTANCE:
			pe->m_fAirResistance = value->Float;
			if((pe->m_fAirResistance + pe->m_fAirResistanceRND) == 0.0f)
				pe->m_iFlags |= PEF_NO_AIR_RESIST;
			else
				pe->m_iFlags &= ~PEF_NO_AIR_RESIST;
			break;

		case EP_AIR_RESISTANCE_RND:
			pe->m_fAirResistanceRND = value->Float;
			if((pe->m_fAirResistance + pe->m_fAirResistanceRND) == 0.0f)
				pe->m_iFlags |= PEF_NO_AIR_RESIST;
			else
				pe->m_iFlags &= ~PEF_NO_AIR_RESIST;
			break;

		case EP_GRAVITY_SCALE:
			pe->m_fGravityMult = value->Float;
			break;

		case EP_GRAVITY_SCALE_RND:
			pe->m_fGravityMultRND = value->Float;
			break;

		case EP_BIRTH_RATE:
			pe->m_fBirthRate = value->Float;
			break;

		case EP_BIRTH_RATE_RND:
			pe->m_fBirthRateRND = value->Float;
			break;

		case EP_LIFETIME:
			pe->m_fLifetime = value->Float;
			break;

		case EP_LIFETIME_RND:
			pe->m_fLifetimeRND = value->Float;
			break;

		case EP_LIFETIME_BY_ANIM:
			break;

		case EP_ANIM_ONCE:
			break;

		case EP_RAND_FRAME:
			break;

		case EP_EFFECT_TIME:
			pe->m_fEmitorTime = value->Float;
			break;

		case EP_REPEAT:
			if(value->Int)
				pe->m_iFlags |= PEF_REPEAT;
			else
				pe->m_iFlags &= ~PEF_REPEAT;
			break;

		case EP_SORT:
			if(value->Int)
				pe->m_iFlags |= PEF_SORT;
			else
				pe->m_iFlags &= ~PEF_SORT;
			break;

		case EP_WIND:
			if(value->Int)
				pe->m_iFlags |= PEF_WIND;
			else
				pe->m_iFlags &= ~PEF_WIND;
			break;

		case EP_CURRENT_TIME:
			pe->m_fTime = value->Float;
			//clamp
			pe->m_fTime = enf_max(pe->m_fTime, 0.0f);
			pe->m_fTime = enf_min(pe->m_fTime, pe->m_fEmitorTime);
			break;
		
		case EP_SPRING:
			pe->m_fSpring = value->Float;
			break;

		default:
			enf_assert(!"Unknown effector parameter");
			break;
		}
	}

	return true;
}

//---------------------------------------------------------------------------
bool ParticleEffectInstance::GetParm(uint n, EmitorParm type, IVariable* value) const
{
	if(n >= ((ParticleEffect*)m_pObject)->m_iNumEmitorDefs)
		return false;

	PEmitor *pe = m_pEmitors[n];

	switch(type)
	{
	case EP_CONEANGLE:
		{
			float *pf = (float*)value->Pointer;
			pf[0] = RAD2DEG(pe->m_vEmitAngle.x);
			pf[1] = RAD2DEG(pe->m_vEmitAngle.y);
			pf[2] = RAD2DEG(pe->m_vEmitAngle.z);
		}
		break;

	case EP_EMITOFFSET:
		*(Vector3*)value->Pointer = pe->m_vEmitOffsets;
		break;

	case EP_VELOCITY:
		value->Float = pe->m_fVelocity;
		break;

	case EP_VELOCITY_RND:
		value->Float = pe->m_fVelocityRND;
		break;

	case EP_AVELOCITY:
		value->Float = RAD2DEG(pe->m_fAngleVelMult);
		break;

	case EP_RANDOM_ROT:
		value->Int = ((pe->m_iFlags & PEF_RANDOM_ROTATION) != 0);
		break;

	case EP_RANDOM_ANGLE:
		value->Int = ((pe->m_iFlags & PEF_RANDOM_ANGLE) != 0);
		break;

	case EP_SIZE:
		value->Float = pe->m_fSize;
		break;

	case EP_STRETCH:
		value->Float = pe->m_fStretch;
		break;

	case EP_AIR_RESISTANCE:
		value->Float = pe->m_fAirResistance;
		break;

	case EP_AIR_RESISTANCE_RND:
		value->Float = pe->m_fAirResistanceRND;
		break;

	case EP_GRAVITY_SCALE:
		value->Float = pe->m_fGravityMult;
		break;

	case EP_GRAVITY_SCALE_RND:
		value->Float = pe->m_fGravityMultRND;
		break;

	case EP_BIRTH_RATE:
		value->Float = pe->m_fBirthRate;
		break;

	case EP_BIRTH_RATE_RND:
		value->Float= pe->m_fBirthRateRND;
		break;

	case EP_LIFETIME:
		value->Float = pe->m_fLifetime;
		break;

	case EP_LIFETIME_RND:
		value->Float = pe->m_fLifetimeRND;
		break;

	case EP_SPRING:
		value->Float = pe->m_fSpring;
		break;

	case EP_LIFETIME_BY_ANIM:
		break;

	case EP_ANIM_ONCE:
		break;

	case EP_RAND_FRAME:
		break;

	case EP_EFFECT_TIME:
		value->Float = pe->m_fEmitorTime;
		break;

	case EP_REPEAT:
		value->Int = ((pe->m_iFlags & PEF_REPEAT) != 0);
		break;

	case EP_SORT:
		value->Int = ((pe->m_iFlags & PEF_SORT) != 0);
		break;

	case EP_WIND:
		value->Int = ((pe->m_iFlags & PEF_WIND) != 0);
		break;

	case EP_CURRENT_TIME:
		value->Float = pe->m_fTime;
		break;

	case EP_ACTIVE_PARTICLES:
		value->Int = pe->m_iNumActiveParticles;
		break;
	}

	return true;
}

//---------------------------------------------------------------------------
bool ParticleEffect::GetParm(uint n, EmitorParm type, IVariable* value) const
{
	if(n >= m_iNumEmitorDefs)
		return false;

	const EmitorDef* pe = &m_pEmitorDefs[n];

	switch(type)
	{
	case EP_CONEANGLE:
		{
			float *pf = (float*)value->Pointer;
			pf[0] = RAD2DEG(pe->m_vEmitAngle.x);
			pf[1] = RAD2DEG(pe->m_vEmitAngle.y);
			pf[2] = RAD2DEG(pe->m_vEmitAngle.z);
		}
		break;

	case EP_EMITOFFSET:
		*(Vector3*)value->Pointer = pe->m_vEmitOffsets;
		break;

	case EP_VELOCITY:
		value->Float = pe->m_fVelocity;
		break;

	case EP_VELOCITY_RND:
		value->Float = pe->m_fVelocityRND;
		break;

	case EP_AVELOCITY:
		value->Float = RAD2DEG(pe->m_fAngleVelMult);
		break;

	case EP_RANDOM_ROT:
		value->Int = ((pe->m_iFlags & PEF_RANDOM_ROTATION) != 0);
		break;

	case EP_RANDOM_ANGLE:
		value->Int = ((pe->m_iFlags & PEF_RANDOM_ANGLE) != 0);
		break;

	case EP_SIZE:
		value->Float = pe->m_fSize;
		break;

	case EP_STRETCH:
		value->Float = pe->m_fStretch;
		break;

	case EP_AIR_RESISTANCE:
		value->Float = pe->m_fAirResistance;
		break;

	case EP_AIR_RESISTANCE_RND:
		value->Float = pe->m_fAirResistanceRND;
		break;

	case EP_GRAVITY_SCALE:
		value->Float = pe->m_fGravityMult;
		break;

	case EP_GRAVITY_SCALE_RND:
		value->Float = pe->m_fGravityMultRND;
		break;

	case EP_BIRTH_RATE:
		value->Float = pe->m_fBirthRate;
		break;

	case EP_BIRTH_RATE_RND:
		value->Float= pe->m_fBirthRateRND;
		break;

	case EP_LIFETIME:
		value->Float = pe->m_fLifetime;
		break;

	case EP_LIFETIME_RND:
		value->Float = pe->m_fLifetimeRND;
		break;

	case EP_SPRING:
		value->Float = pe->m_fSpring;
		break;

	case EP_LIFETIME_BY_ANIM:
		break;

	case EP_ANIM_ONCE:
		break;

	case EP_RAND_FRAME:
		break;

	case EP_EFFECT_TIME:
		value->Float = pe->m_fEmitorTime;
		break;

	case EP_REPEAT:
		value->Int = ((pe->m_iFlags & PEF_REPEAT) != 0);
		break;

	case EP_SORT:
		value->Int = ((pe->m_iFlags & PEF_SORT) != 0);
		break;

	case EP_WIND:
		value->Int = ((pe->m_iFlags & PEF_WIND) != 0);
		break;

	case EP_CURRENT_TIME:
		value->Float = 0;
		break;

	case EP_ACTIVE_PARTICLES:
		value->Int = 0;
		break;
	}

	return true;
}

//---------------------------------------------------------------------------
uint ParticleEffect::GetEmitorIndex(const char* name) const
{
	for(uint n = 0; n < m_iNumEmitorDefs; n++)
	{
		if(m_pEmitorDefs[n].m_strName.cmpi(name) == 0)
			return n;
	}
	return INDEX_NOT_FOUND;
}

//---------------------------------------------------------------------------
uint ParticleEffect::GetEmitorNames(const char **names, uint maxnum) const
{
	if(names)
	{
		for(uint n = 0; n < m_iNumEmitorDefs && n < maxnum; n++)
			names[n] = m_pEmitorDefs[n].m_strName.cstr();
	}

	return m_iNumEmitorDefs;
}

const char* LoadParticleEffectResource::m_GraphNames[] = {
	NULL,
	"ColorR",
	"ColorG",
	"ColorB",
	"Alpha",
	"ColorRMast",
	"ColorGMast",
	"ColorBMast",
	"AlphaMast",
	"Size",
	"SizeMast",
	"RotationSpeed",
	"RotationSpeedMast",
	"BRateMast"
};

//-----------------------------------------------------------------------------
LoadParticleEffectResource::LoadParticleEffectResource(ParticleEffect* obj): LoadVObjectResource(obj),
	m_pEmitorDefs(NULL),
	m_iNumEmitorDefs(0),
	m_fVelocityAffection(0),
	m_iMemorySize(0)
{
	obj->AddRef();
}

//-----------------------------------------------------------------------------
LoadParticleEffectResource::~LoadParticleEffectResource()
{
	if(m_pResource)
		m_pResource->FinishLoading(this);
	bool hasObject = GetObject() != NULL;

	for(uint e = 0; e < m_iNumEmitorDefs; e++)
	{
		for(uint n = 0; n < GT_SIZEOF; n++)
		{
			SAFE_DELETE(m_pEmitorDefs[e].m_Graphs[n]);
		}
		if(m_pEmitorDefs[e].m_pMaterial)
		{
			if(hasObject)
				m_pEmitorDefs[e].m_pMaterial->Unload();
			m_pEmitorDefs[e].m_pMaterial->Release();
		}
	}
	SAFE_DELETE_ARRAY(m_pEmitorDefs);
	if(hasObject)
		GetObject()->Release();
}

//-----------------------------------------------------------------------------
bool LoadParticleEffectResource::DoLoad()
{
	CFileInputStream instream(GetObject()->GetFileHandle());
	if(!instream.IsOpen())
		return false;

	return Load(&instream);
}

//-----------------------------------------------------------------------------
#ifdef ENF_DEVELOPER
enum ParamType
{
	PT_BOOL,
	PT_FLOAT,
	PT_INT,
	PT_VECTOR3,
	PT_VECTOR2,
	PT_GRAPH,
	PT_STRING
};

class WriteBinary
{
	CompactArray<CStr> strings;
	IFFOutput& iff;
	bool BigEndian;

	void WriteBEFloats(const float* ptr, uint numElems)
	{
		float* dest = ALLOCA(float, numElems);
		for(uint n = 0; n < numElems; n++)
			dest[n] = BE_FLOAT(ptr[n]);
		iff.Write(dest, sizeof(float) * numElems);
	}
	void WriteBEInts(const int* ptr, uint numElems)
	{
		int* dest = ALLOCA(int, numElems);
		for(uint n = 0; n < numElems; n++)
			dest[n] = BE_INT(ptr[n]);
		iff.Write(dest, sizeof(int) * numElems);
	}

public:
	WriteBinary(IFFOutput& _iff, bool bigEndian): iff(_iff), BigEndian(bigEndian)
	{
	}
	uint AddString(const char* str)
	{
		return strings.Insert(str);
	}
	uint WriteStrings()
	{
		uint size = 0;
		for(uint n = 0; n < strings.GetCardinality(); n++)
		{
			uint slen = strings[n].Length() + 1;
			iff.Write(strings[n].cstr(), slen);
			size += slen;
		}
		return size;
	}

	bool WriteParam(const char* paramName, ParamType type, uint numElems, const void* pdata)
	{
		uint n;
		for(n = 0; n < numElems; n++)
		{
			if(type == PT_BOOL && ((const bool*)pdata)[n])
				break;
			else if(type == PT_FLOAT && ((const float*)pdata)[n])
				break;
			else if(type == PT_INT && ((const int*)pdata)[n])
				break;
			else if(type == PT_VECTOR3 && !((const Vector3*)pdata)[n].IsZero())
				break;
			else if(type == PT_VECTOR2 && !((const Vector2*)pdata)[n].IsZero())
				break;
			else if(type == PT_VECTOR2 && ((const Vector2*)pdata)[n].y != 1.0f)
				break;
			else if(type == PT_STRING && !CStr(((const char**)pdata)[n]).IsEmpty())
				break;
			else if(type == PT_GRAPH && ((const Pair<float,float>*)pdata)[n].Data != 1.0f)
				break;
		}
		if(n == numElems)
			return false;

		ParticleFileParam param;
		param.ParamName = AddString(paramName);
		param.NumElems = numElems;
		if(BigEndian)
		{
			param.ParamName = BE_SHORT(param.ParamName);
			param.NumElems = BE_SHORT(param.NumElems);
		}
		iff.Write(&param, sizeof(param));
		switch(type)
		{
		case PT_BOOL:
			iff.Write(pdata, sizeof(bool) * numElems);
			break;
		case PT_FLOAT:
			if(BigEndian)
				WriteBEFloats((const float*)pdata, numElems);
			else
				iff.Write(pdata, sizeof(float) * numElems);
			break;
		case PT_INT:
			if(BigEndian)
				WriteBEInts((const int*)pdata, numElems);
			else
				iff.Write(pdata, sizeof(int) * numElems);
			break;
		case PT_VECTOR3:
			if(BigEndian)
				WriteBEFloats((const float*)pdata, numElems * 3);
			else
				iff.Write(pdata, sizeof(float) * 3 * numElems);
			break;
		case PT_VECTOR2:
			if(BigEndian)
				WriteBEFloats((const float*)pdata, numElems * 2);
			else
				iff.Write(pdata, sizeof(float) * 2 * numElems);
			break;
		case PT_GRAPH:
			if(BigEndian)
				WriteBEFloats((const float*)pdata, numElems * 2);
			else
				iff.Write(pdata, sizeof(float) * 2 * numElems);
			break;
		case PT_STRING:
			for(n = 0; n < numElems; n++)
			{
				ushort index = AddString(((const char**)pdata)[n]);
				if(BigEndian)
					index = BE_SHORT(index);
				iff.Write(&index, sizeof(index));
			}
			break;
		};
		return true;
	}
};


//-----------------------------------------------------------------------------
void LoadParticleEffectResource::Save(COutputStream* outstream, bool be)
{
	IFFOutput iff(outstream, HunkID("PTC1"));
	WriteBinary writer(iff, be);

	iff.BeginHunk(HunkID("DATA"));

	ParticleFileHeader header;
	header.NumEmitors = m_iNumEmitorDefs;
	header.NumParams = m_fVelocityAffection == 0 ? 0 : 1;
	if(be)
	{
		header.NumEmitors = BE_SHORT(header.NumEmitors);
		header.NumParams = BE_SHORT(header.NumParams);
	}
	iff.Write(&header, sizeof(header));

	writer.WriteParam("VelAffection", PT_FLOAT, 1, &m_fVelocityAffection);

	for(uint n = 0; n < m_iNumEmitorDefs; n++)
	{
		const Emitor& em = m_pEmitorDefs[n];
		
		size_t emitorPos = iff.GetCurrentPosition();
		ParticleFileEmitor emitor;
		emitor.EmitorName = writer.AddString(em.m_strName.cstr());
		emitor.NumParams = 0;
		iff.Write(&emitor, sizeof(emitor));

		const char* matName = em.m_pMaterial->GetName().cstr();
		if(writer.WriteParam("Material", PT_STRING, 1, &matName))
			emitor.NumParams++;
		int iv;
		bool bv;
		iv = (int)em.m_eEmitorType;
		if(writer.WriteParam("ShapeType", PT_INT, 1, &iv))
			emitor.NumParams++;
		if(writer.WriteParam("ShapeSize", PT_VECTOR3, 1, em.m_vEmitShape.v))
			emitor.NumParams++;
		if(writer.WriteParam("Angles", PT_VECTOR3, 1, em.m_vEmitAngles.v))
			emitor.NumParams++;
		if(writer.WriteParam("Offset", PT_VECTOR3, 1, em.m_vEmitOffsets.v))
			emitor.NumParams++;
		if(writer.WriteParam("ConeAngle", PT_VECTOR3, 1, em.m_vEmitAngle.v))
			emitor.NumParams++;
		if(writer.WriteParam("Velocity", PT_FLOAT, 1, &em.m_fVelocity))
			emitor.NumParams++;
		if(writer.WriteParam("VelocityRND", PT_FLOAT, 1, &em.m_fVelocityRND))
			emitor.NumParams++;
		if(writer.WriteParam("RotMultiplier", PT_FLOAT, 1, &em.m_fAngleVelMult))
			emitor.NumParams++;
		if(writer.WriteParam("SizeMultiplier", PT_FLOAT, 1, &em.m_fSize))
			emitor.NumParams++;
		if(writer.WriteParam("StretchMultiplier", PT_FLOAT, 1, &em.m_fStretch))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_RANDOM_ROTATION) != 0;
		if(writer.WriteParam("RandomRotDir", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_RANDOM_ANGLE) != 0;
		if(writer.WriteParam("RandomAngle", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		if(em.m_fRestitution > 0 && writer.WriteParam("Restitution", PT_FLOAT, 1, &em.m_fRestitution))
			emitor.NumParams++;
		if(writer.WriteParam("LifetimeShortening", PT_FLOAT, 1, &em.m_fLifetimeShortening))
			emitor.NumParams++;
		if(writer.WriteParam("AirResistance", PT_FLOAT, 1, &em.m_fAirResistance))
			emitor.NumParams++;
		if(writer.WriteParam("AirResistanceRND", PT_FLOAT, 1, &em.m_fAirResistanceRND))
			emitor.NumParams++;
		if(writer.WriteParam("GravityMultiply", PT_FLOAT, 1, &em.m_fGravityMult))
			emitor.NumParams++;
		if(writer.WriteParam("GravityMultiplyRND", PT_FLOAT, 1, &em.m_fGravityMultRND))
			emitor.NumParams++;
		if(writer.WriteParam("Spring", PT_FLOAT, 1, &em.m_fSpring))
			emitor.NumParams++;
		if(writer.WriteParam("LOD", PT_INT, 1, &em.m_iLOD))
			emitor.NumParams++;
		if(writer.WriteParam("MaxNum", PT_INT, 1, &em.m_iMaxParticles))
			emitor.NumParams++;
		if(writer.WriteParam("BirthRate", PT_FLOAT, 1, &em.m_fBirthRate))
			emitor.NumParams++;
		if(writer.WriteParam("BirthRateRND", PT_FLOAT, 1, &em.m_fBirthRateRND))
			emitor.NumParams++;
		if(writer.WriteParam("BirthRateVel", PT_FLOAT, 1, &em.m_fBirthRateVel))
			emitor.NumParams++;
		if(writer.WriteParam("LifeTime", PT_FLOAT, 1, &em.m_fLifetime))
			emitor.NumParams++;
		if(writer.WriteParam("LifeTimeRND", PT_FLOAT, 1, &em.m_fLifetimeRND))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_LIFETIME_BY_ANIM) != 0;
		if(writer.WriteParam("LifetimeByAnim", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_ANIM_ONCE) != 0;
		if(writer.WriteParam("AnimOnce", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_RANDOM_FRAME) != 0;
		if(writer.WriteParam("RandFrame", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_VEL_ANGLE) != 0;
		if(writer.WriteParam("VelAngle", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		if(writer.WriteParam("AnimFPS", PT_FLOAT, 1, &em.m_fAnimFPS))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_POINTSPRITE) != 0;
		if(writer.WriteParam("PointSprites", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		if(writer.WriteParam("EffectTime", PT_FLOAT, 1, &em.m_fEmitorTime))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_REPEAT) != 0;
		if(writer.WriteParam("Repeat", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_SORT) != 0;
		if(writer.WriteParam("Sort", PT_BOOL, 1, &bv))
			emitor.NumParams++;
		bv = (em.m_iFlags & PEF_WIND) != 0;
		if(writer.WriteParam("Wind", PT_BOOL, 1, &bv))
			emitor.NumParams++;


		for(uint g = 0; g < GT_SIZEOF; g++)
		{
			if(m_GraphNames[g])
			{
				bool wrote;
				if(em.m_Graphs[g])
					wrote = writer.WriteParam(m_GraphNames[g], PT_GRAPH, em.m_Graphs[g]->GetCardinality(), em.m_Graphs[g]->GetArray());
				else
				{
					float gr[2] = {0,0};
					gr[1] = em.m_Constants[g];
					wrote = writer.WriteParam(m_GraphNames[g], PT_GRAPH, 1, gr);
				}
				if(wrote) emitor.NumParams++;
			}
		}

		size_t lastPos = iff.GetCurrentPosition();
		iff.Seek(emitorPos);
		if(be)
		{
			emitor.EmitorName = BE_SHORT(emitor.EmitorName);
			emitor.NumParams = BE_SHORT(emitor.NumParams);
		}
		iff.Write(&emitor, sizeof(emitor));
		iff.Seek(lastPos);
	}
	iff.EndHunk();
	iff.BeginHunk(HunkID("STRT"));
	writer.WriteStrings();
	iff.EndHunk();
}
#endif

//-----------------------------------------------------------------------------
LoadResource* ParticleEffect::CreateResourceLoader()
{
	return ENF_NEW LoadParticleEffectResource(this);
}

//-----------------------------------------------------------------------------
void Read(void*& ptr, void* data, uint size)
{
	memcpy(data, ptr, size);
	ptr = (void*)((char*)ptr + size);
}
template<class T> void Read(void*& ptr, T& data)
{
	Read(ptr, &data, sizeof(T));
}

ENF_INLINE uint ReadBoolFlag(void*& ptr, uint flag)
{
	bool b;
	Read(ptr, b);
	return b ? flag : 0;
}

//-----------------------------------------------------------------------------
bool LoadParticleEffectResource::Load(CInputStream* instream)
{
	SArray<Emitor, MAX_PARTICLE_EMITORS> emitors;
	SString<64> materialnames[MAX_PARTICLE_EMITORS];
	EMIT_GRAPH_TYPE gtype;

	size_t startPos = instream->GetCurrentPosition();
	Emitor* emit = NULL;
	bool res = false;
	bool loadMat = GetObject() != NULL;

	HunkID id;
	IFFInput iff(instream, id);
	if(iff.IsOpen() && id == HunkID("PTC1"))
	{
		void* ptr = NULL;
		const char** strings = NULL;

		size_t size;
		while((size = iff.BeginHunk(id)) != -1)
		{
			if(id == "DATA")
			{
				ptr = ALLOCA(char, size);
				iff.Read(ptr, size);
			}
			else if(id == "STRT")
			{
				char* strTab = ALLOCA(char, size);
				iff.Read(strTab, size);
				uint numStrings = CountStrings(strTab, size);
				strings = ALLOCA(const char*, numStrings);
				InitStringPool(strings, strTab, numStrings);
			}

			iff.EndHunk();
		}

		if(ptr == NULL || strings == NULL)
		{
			res = false;
		}
		else
		{
			res = true;

			ParticleFileHeader header;
			Read(ptr, header);
			for(uint n = 0; n < header.NumParams; n++)
			{
				ParticleFileParam param;
				Read(ptr, param);

				CStr paramName = strings[param.ParamName];

				if(paramName == "VelAffection")
					Read(ptr, m_fVelocityAffection);
			}

			for(uint n = 0; n < header.NumEmitors; n++)
			{
				ParticleFileEmitor emitor;
				Read(ptr, emitor);

				emit = &emitors.Append();
				ZeroMem(emit, sizeof(*emit));
				for(uint n = 0; n < GT_SIZEOF; n++)
					emit->m_Constants[n] = 1.0f;

				m_iMemorySize += sizeof(Emitor);

				emit->m_strName = strings[emitor.EmitorName];

				for(uint p = 0; p < emitor.NumParams; p++)
				{
					ParticleFileParam param;
					Read(ptr, param);
					CStr paramName = strings[param.ParamName];

					if(paramName == "Material")
					{
						enf_assert(param.NumElems == 1);
						ushort str;
						Read(ptr, str);
						materialnames[emitors.GetCardinality()-1] = strings[str];
						continue;
					}

					if(paramName == "ShapeType")
					{
						enf_assert(param.NumElems == 1);
						int type;
						Read(ptr, type);
						emit->m_eEmitorType = (EmitorDef::EmitorType)type;
						continue;
					}

					if(paramName == "ShapeSize")
					{
						enf_assert(param.NumElems == 1);
						float emitSize[3];
						Read(ptr, emitSize);
						emit->m_vEmitShape = emitSize;
						continue;
					}

					if(paramName == "ConeAngle")
					{
						enf_assert(param.NumElems == 1);
						float emitAngles[3];
						Read(ptr, emitAngles);
						emit->m_vEmitAngle = emitAngles;
						continue;
					}

					if(paramName == "Offset")
					{
						enf_assert(param.NumElems == 1);
						float emitOffset[3];
						Read(ptr, emitOffset);
						emit->m_vEmitOffsets = emitOffset;
						continue;
					}

					if(paramName == "Angles")
					{
						enf_assert(param.NumElems == 1);
						float emitAngles[3];
						Read(ptr, emitAngles);
						emit->m_vEmitAngles = emitAngles;
						continue;
					}


					if(paramName == "Velocity")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fVelocity);
						continue;
					}

					if(paramName == "VelocityRND")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fVelocityRND);
						continue;
					}

					if(paramName == "RotMultiplier")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fAngleVelMult);
						continue;
					}

					if(paramName == "SizeMultiplier")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fSize);
						continue;
					}

					if(paramName == "StretchMultiplier")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fStretch);
						continue;
					}

					if(paramName == "RandomRotDir")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_RANDOM_ROTATION);
						continue;
					}

					if(paramName == "AirResistance")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fAirResistance);
						continue;
					}

					if(paramName == "AirResistanceRND")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fAirResistanceRND);
						continue;
					}

					if(paramName == "GravityMultiply")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fGravityMult);
						continue;
					}

					if(paramName == "GravityMultiplyRND")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fGravityMultRND);
						continue;
					}

					if(paramName == "MaxNum")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_iMaxParticles);
						res &= (emit->m_iMaxParticles != 0);
						continue;
					}

					if(paramName == "PointSprites")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_POINTSPRITE);
						continue;
					}

					if(paramName == "BirthRate")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fBirthRate);
						continue;
					}

					if(paramName == "BirthRateRND")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fBirthRateRND);
						continue;
					}

					if(paramName == "BirthRateVel")
					{
						enf_assert(param.NumElems == 1);
						Read(ptr, emit->m_fBirthRateVel);
						continue;
					}

					if(paramName == "LifeTime")
					{
						Read(ptr, emit->m_fLifetime);
						continue;
					}

					if(paramName == "LifeTimeRND")
					{
						Read(ptr, emit->m_fLifetimeRND);
						continue;
					}

					if(paramName == "LifetimeByAnim")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_LIFETIME_BY_ANIM);
						continue;
					}

					if(paramName == "RandomAngle")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_RANDOM_ANGLE);
						continue;
					}

					if(paramName == "AnimOnce")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_ANIM_ONCE);
						continue;
					}

					if(paramName == "RandFrame")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_RANDOM_FRAME);
						continue;
					}

					if(paramName == "VelAngle")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_VEL_ANGLE);
						continue;
					}

					if(paramName == "Spring")
					{
						Read(ptr, emit->m_fSpring);
						continue;
					}

					if(paramName == "LOD")
					{
						Read(ptr, emit->m_iLOD);
						continue;
					}

					if(paramName == "Repeat")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_REPEAT);
						continue;
					}

					if(paramName == "Sort")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_SORT);
						continue;
					}

					if(paramName == "Wind")
					{
						emit->m_iFlags |= ReadBoolFlag(ptr, PEF_WIND);
						continue;
					}

					if(paramName == "Restitution")
					{
						Read(ptr, emit->m_fRestitution);
						if(emit->m_fRestitution > 0)
							emit->m_iFlags |= PEF_COLLIDING;

						continue;
					}

					if(paramName == "LifetimeShortening")
					{
						Read(ptr, emit->m_fLifetimeShortening);
						continue;
					}

					if(paramName == "EffectTime")
					{
						Read(ptr, emit->m_fEmitorTime);
						continue;
					}

					if(paramName == "AnimFPS")
					{
						Read(ptr, emit->m_fAnimFPS);
						continue;
					}

					
					uint g;
					uint maxGraphs = sizeof(m_GraphNames) / sizeof(m_GraphNames[0]);
					for(g = 1; g < maxGraphs; g++)
					{
						if(paramName == m_GraphNames[g])
						{
							if(param.NumElems == 1)
							{
								float graph[2];
								Read(ptr, graph);
								emit->m_Constants[g] = graph[1];
							}
							else
							{
								TSrcGraph* pgraph = ENF_NEW TSrcGraph;
								pgraph->SetSize(param.NumElems);
								Read(ptr, pgraph->GetArray(), param.NumElems * sizeof(Pair<float, float>));
								emit->m_Graphs[g] = pgraph;
								m_iMemorySize += sizeof(TSrcGraph);
							}
							break;
						}
					}

					enf_assert(g != maxGraphs);
				}
			}
		}
	}
	else
	{
		res = true;
		instream->Seek(startPos);

		CParser tp;
		bool first = true;
		uint graphSize = 0;

		TSrcGraph graph;
		if(tp.Parse(instream) != PE_OK)
			return false;

		for(uint l = 0; l < tp.GetNumLines() && res; l++)
		{
			if(!first)
			{
				if(!tp.NextLine(false))
					break;
			}
			else
				first = false;

			if(graphSize)
			{
				float gt, gv;
				res &= ParseFloatNQ(tp, gt, false);
				res &= ParseFloatNQ(tp, gv);
				graph.Insert(MakePair(gt, gv));

				if(graph.GetCardinality() == graphSize)
				{
					if(gtype != UNUSED)
					{
						uint n = 1;
						for(;n < graphSize; n++)
						{
							if(graph[0].Data != graph[n].Data)
								break;
						}
						bool constant = n == graphSize;

						if(!constant)
						{
							TSrcGraph* pgraph = ENF_NEW TSrcGraph;
							*pgraph = graph;
							emit->m_Graphs[gtype] = pgraph;
							m_iMemorySize += sizeof(TSrcGraph);
						}
						else
						{
							emit->m_Constants[gtype] = graph[0].Data;
						}
					}
					graphSize = 0;
					graph.Clear();
				}

				continue;
			}

			ISTOKEN(tp, "VelAffection")
			{
				res &= ParseFloat(tp, m_fVelocityAffection);
				continue;
			}

			ISTOKEN(tp, "$EmitorDef")
			{
				if(!tp.NextToken(true))
				{
					res = false;
					continue;
				}

				if(emitors.IsFull())
				{
					break;
				}
				emit = &emitors.Append();
				ZeroMem(emit, sizeof(*emit));

				m_iMemorySize += sizeof(Emitor);

				if(tp.AssertType(TT_QUOTE))
				{
					emit->m_strName = tp.CToken->Text;
					continue;
				}
				res = false;
				continue;
			}

			if(!emit)
			{
				tp.LError(CParser::SequenceError);
				res = false;
				break;
			}

			ISTOKEN(tp, "Shader")
			{
				if(!tp.NextToken(true))
				{
					res = false;
					continue;
				}
				if(tp.AssertType(TT_QUOTE))
				{
					materialnames[emitors.GetCardinality()-1] = tp.CToken->Text;
					continue;
				}
				res = false;
				continue;
			}

			ISTOKEN(tp, "ShapeType")
			{
				uint type;
				if(ParseInt(tp, type))
				{
					switch(type)
					{
					case 0:
						emit->m_eEmitorType = EmitorDef::POINT;
						break;

					case 1:
						emit->m_eEmitorType = EmitorDef::BOX;
						break;

					case 2:
						emit->m_eEmitorType = EmitorDef::SPHERE;
						break;

					case 3:
						emit->m_eEmitorType = EmitorDef::ELIPSE;
						break;
					}
					continue;
				}

				res = false;
				continue;
			}

			ISTOKEN(tp, "ShapeSize")
			{
				res &= ParseVector(tp, emit->m_vEmitShape);
				continue;
			}

			ISTOKEN(tp, "KillShapeType")
			{
				continue;
			}

			ISTOKEN(tp, "KillShapeSize")
			{
				continue;
			}

			ISTOKEN(tp, "ConeAngle")
			{
				res &= ParseVector(tp, emit->m_vEmitAngle);
				continue;
			}

			ISTOKEN(tp, "Offset")
			{
				res &= ParseVector(tp, emit->m_vEmitOffsets);
				continue;
			}

			ISTOKEN(tp, "Angles")
			{
				res &= ParseVector(tp, emit->m_vEmitAngles);
				continue;
			}

			ISTOKEN(tp, "Velocity")
			{
				res &= ParseFloat(tp, emit->m_fVelocity);
				continue;
			}

			ISTOKEN(tp, "VelocityRND")
			{
				res &= ParseFloat(tp, emit->m_fVelocityRND);
				continue;
			}

			ISTOKEN(tp, "RotMultiplier")
			{
				res &= ParseFloat(tp, emit->m_fAngleVelMult);
				continue;
			}

			ISTOKEN(tp, "SizeMultiplier")
			{
				res &= ParseFloat(tp, emit->m_fSize);
				continue;
			}

			ISTOKEN(tp, "StretchMultiplier")
			{
				res &= ParseFloat(tp, emit->m_fStretch);
				continue;
			}

			ISTOKEN(tp, "RandomRotDir")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_RANDOM_ROTATION);
				continue;
			}

			ISTOKEN(tp, "AirResistance")
			{
				res &= ParseFloat(tp, emit->m_fAirResistance);
				continue;
			}

			ISTOKEN(tp, "AirResistanceRND")
			{
				res &= ParseFloat(tp, emit->m_fAirResistanceRND);
				continue;
			}

			ISTOKEN(tp, "GravityMultiply")
			{
				res &= ParseFloat(tp, emit->m_fGravityMult);
				continue;
			}

			ISTOKEN(tp, "GravityMultiplyRND")
			{
				res &= ParseFloat(tp, emit->m_fGravityMultRND);
				continue;
			}

			ISTOKEN(tp, "MaxNum")
			{
				res &= ParseInt(tp, emit->m_iMaxParticles);
				res &= (emit->m_iMaxParticles != 0);
				continue;
			}

			ISTOKEN(tp, "KillOldest")
			{
				continue;
			}

			ISTOKEN(tp, "PointSprites")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_POINTSPRITE);
				continue;
			}

			ISTOKEN(tp, "BirthRate")
			{
				res &= ParseFloat(tp, emit->m_fBirthRate);
				continue;
			}

			ISTOKEN(tp, "BirthRateRND")
			{
				res &= ParseFloat(tp, emit->m_fBirthRateRND);
				continue;
			}

			ISTOKEN(tp, "BirthRateVel")
			{
				res &= ParseFloat(tp, emit->m_fBirthRateVel);
				continue;
			}

			ISTOKEN(tp, "LifeTime")
			{
				res &= ParseFloat(tp, emit->m_fLifetime);
				continue;
			}

			ISTOKEN(tp, "LifeTimeRND")
			{
				res &= ParseFloat(tp, emit->m_fLifetimeRND);
				continue;
			}

			ISTOKEN(tp, "LifetimeByAnim")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_LIFETIME_BY_ANIM);
				continue;
			}

			ISTOKEN(tp, "RandomAngle")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_RANDOM_ANGLE);
				continue;
			}

			ISTOKEN(tp, "AnimOnce")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_ANIM_ONCE);
				continue;
			}

			ISTOKEN(tp, "RandFrame")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_RANDOM_FRAME);
				continue;
			}

			ISTOKEN(tp, "VelAngle")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_VEL_ANGLE);
				continue;
			}

			ISTOKEN(tp, "Spring")
			{
				res &= ParseFloat(tp, emit->m_fSpring);
				continue;
			}

			ISTOKEN(tp, "Fluid")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_FLUID);
				continue;
			}
			ISTOKEN(tp, "ColorB")
			{
				res &= ParseIntNQ(tp, graphSize);
				gtype = COLOR_B;
				continue;
			}

			ISTOKEN(tp, "Stretch")
			{
				res &= ParseIntNQ(tp, graphSize);
				gtype = UNUSED;
				continue;
			}

			ISTOKEN(tp, "LOD")
			{
				res &= ParseInt(tp, emit->m_iLOD);
				continue;
			}

			ISTOKEN(tp, "Repeat")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_REPEAT);
				continue;
			}

			ISTOKEN(tp, "Sort")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_SORT);
				continue;
			}

			ISTOKEN(tp, "Wind")
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_WIND);
				continue;
			}

			ISTOKEN(tp, "Collisions") //Legacy support
			{
				res &= ParseFlag(tp, emit->m_iFlags, PEF_COLLIDING);
				if(emit->m_iFlags & PEF_COLLIDING)
					emit->m_fRestitution = 0.7f;

				continue;
			}

			ISTOKEN(tp, "Restitution")
			{
				res &= ParseFloat(tp, emit->m_fRestitution);
				if(emit->m_fRestitution > 0)
					emit->m_iFlags |= PEF_COLLIDING;
				
				continue;
			}

			ISTOKEN(tp, "LifetimeShortening")
			{
				res &= ParseFloat(tp, emit->m_fLifetimeShortening);
				continue;
			}

			ISTOKEN(tp, "EffectTime")
			{
				res &= ParseFloat(tp, emit->m_fEmitorTime);
				continue;
			}

			ISTOKEN(tp, "AnimFPS")
			{
				res &= ParseFloat(tp, emit->m_fAnimFPS);
				continue;
			}

			ISTOKEN(tp, "StretchMast")
			{
				res &= ParseIntNQ(tp, graphSize);
				gtype = UNUSED;
				continue;
			}

			uint maxGraphs = sizeof(m_GraphNames) / sizeof(m_GraphNames[0]);
			uint n;
			for(n = 0; n < maxGraphs; n++)
			{
				if(CStr(m_GraphNames[n]) == tp.CToken->Text)
				{
					gtype = (EMIT_GRAPH_TYPE)n;
					break;
				}
			}
			if(n < maxGraphs)
			{
				res &= ParseIntNQ(tp, graphSize);
				continue;
			}
			//just ignore it
		}
	}

	if(res)
	{
		for(uint n = 0; n < emitors.GetCardinality(); n++)
		{
			uint mcflags = 0;

			if(emitors[n].m_iFlags & PEF_POINTSPRITE)
				if (emitors[n].m_fStretch <= 1.0f) 
					mcflags = MCF_PARTICLE_POINT;
				else
					mcflags = MCF_PARTICLE_STREAK;
			else
				mcflags = MCF_PARTICLE_SPRITE;

			emitors[n].m_pMaterial = GetMaterialSystem()->GetMaterial(materialnames[n].cstr(), mcflags);
			if(emitors[n].m_pMaterial)
			{
				if(loadMat)
					emitors[n].m_pMaterial->Load();
			}
			else
			{
				res = false;
				enf::LogF(LL_ERROR, "ParticleEffect %s cannot found material %s", GetName().cstr(), materialnames[n].cstr());
			}
		}
	}

	if(res)
	{
		m_pEmitorDefs = ENF_NEW Emitor[emitors.GetCardinality()];
		m_iNumEmitorDefs = emitors.GetCardinality();
		memcpy(m_pEmitorDefs, emitors.GetArray(), sizeof(Emitor) * emitors.GetCardinality());
	}
	else
	{
		for(uint n = 0; n < emitors.GetCardinality(); n++)
		{
			if(emitors[n].m_pMaterial)
			{
				if(loadMat)
					emitors[n].m_pMaterial->Unload();
				emitors[n].m_pMaterial->Release();
				emitors[n].m_pMaterial = NULL;
			}
		}
	}
	return res;
}

//---------------------------------------------------------------------------
void FillTexRow(ParamTextureItem* data, const TSrcGraph* graph, float constant)
{
	if(graph)
	{
		float f = (float) (PARAM_TEXTURE_WIDTH - 1);

		//get graph values
		for (uint i = 0; i < PARAM_TEXTURE_WIDTH; ++i)
		{
			float n = enf_limit(GetCurvedValue(*graph, i/f), 0.0f, 1.0f);
 
			*data = n*PARAM_MULTIPLY;
			data += 4;
		}
	}
	else
	{
		constant = enf_limit(constant, 0.0f, 1.0f);

		ParamTextureItem c = constant*PARAM_MULTIPLY;

		for (uint i = 0; i < PARAM_TEXTURE_WIDTH; ++i)
		{
			*data = c;
			data += 4;
		}
	}
}
//---------------------------------------------------------------------------
void LoadParticleEffectResource::Apply()
{
	ParticleEffect* obj = GetObject();

	obj->m_iNumEmitorDefs = m_iNumEmitorDefs;
	obj->m_pEmitorDefs = ENF_NEW EmitorDef[m_iNumEmitorDefs];
	obj->m_iAllFlags = 0;

	AAB bbox;
	bbox.Zero();

	for(uint n = 0; n < m_iNumEmitorDefs; n++)
	{
		EmitorDef* emit = &obj->m_pEmitorDefs[n];
		const Emitor* semit = &m_pEmitorDefs[n];

		emit->m_eEmitorType = semit->m_eEmitorType;
		emit->m_vEmitShape = semit->m_vEmitShape;

		emit->m_fVelocity = semit->m_fVelocity;
		emit->m_fVelocityRND = semit->m_fVelocityRND;

		emit->m_vEmitOffsets = semit->m_vEmitOffsets;
		emit->m_vEmitAngles = semit->m_vEmitAngles;

		emit->m_vEmitAngle = semit->m_vEmitAngle;
		emit->m_fGravityMult = semit->m_fGravityMult;
		emit->m_fGravityMultRND = semit->m_fGravityMultRND;

		emit->m_fSize = semit->m_fSize;

		//create local AAB of emitor
		AAB lbox;
		switch(emit->m_eEmitorType)
		{
		case EmitorDef::POINT:
			lbox.Init(emit->m_vEmitOffsets);
			break;
		case EmitorDef::BOX:
		case EmitorDef::ELIPSE:
			lbox.Init(emit->m_vEmitOffsets - emit->m_vEmitShape, emit->m_vEmitOffsets + emit->m_vEmitShape);
			break;
		case EmitorDef::SPHERE:
			{
				Vector3 off(emit->m_vEmitShape.x);
				lbox.Init(emit->m_vEmitOffsets - off, emit->m_vEmitOffsets + off);
			}
			break;
		};
		Vector3 size(emit->m_fSize);
		lbox.AddMin(lbox.GetMins() - size);
		lbox.AddMax(lbox.GetMaxs() + size);

		bbox.AddMin(lbox.GetMins());
		bbox.AddMax(lbox.GetMaxs());

		emit->m_fStretch = semit->m_fStretch;
		emit->m_fLifetime = semit->m_fLifetime;
		emit->m_fLifetimeRND = semit->m_fLifetimeRND;
		emit->m_iMaxParticles = semit->m_iMaxParticles;
		emit->m_fBirthRate = semit->m_fBirthRate;
		emit->m_fBirthRateRND = semit->m_fBirthRateRND;
		emit->m_fBirthRateVel = semit->m_fBirthRateVel;
		emit->m_fRestitution = semit->m_fRestitution;
		emit->m_fLifetimeShortening = semit->m_fLifetimeShortening;
		emit->m_iFlags = semit->m_iFlags;
		emit->m_fEmitorTime = semit->m_fEmitorTime;
		emit->m_fSpring = semit->m_fSpring;
		emit->m_iLOD = semit->m_iLOD;

		emit->m_iTilesCount = semit->m_iTilesCount;
		emit->m_fAnimFPS = semit->m_fAnimFPS;

		emit->m_fAngleVelMult = semit->m_fAngleVelMult;
		emit->m_fAirResistance = semit->m_fAirResistance;
		emit->m_fAirResistanceRND = semit->m_fAirResistanceRND;

		emit->m_pMaterial = semit->m_pMaterial;
		if(emit->m_pMaterial)
		{
			emit->m_pMaterial->Load();
			emit->m_pMaterial->AddRef();
		}
		emit->m_strName = CStr(semit->m_strName.cstr()).Static();

		if(semit->m_Graphs[ANGLE] == NULL)
			emit->m_fAngleVelConstant = semit->m_Constants[ANGLE];
		else
		{
			emit->m_pAngleVelGraph = ENF_NEW EmitorDef::FloatGraph;
			FillGraph(emit->m_pAngleVelGraph->m_Graph, 1, *semit->m_Graphs[ANGLE]);
		}

		if(semit->m_Graphs[MANGLE] == NULL)
			emit->m_fMAngleVelConstant = semit->m_Constants[MANGLE];
		else
		{
			emit->m_pMAngleVelGraph = ENF_NEW EmitorDef::FloatGraph;
			FillGraph(emit->m_pMAngleVelGraph->m_Graph, 1, *semit->m_Graphs[MANGLE]);
		}

		if(semit->m_Graphs[MBIRTHRATE] == NULL)
			emit->m_fMBirthRateConstant = semit->m_Constants[MBIRTHRATE];
		else
		{
			emit->m_pMBirthRateGraph = ENF_NEW EmitorDef::FloatGraph;
			FillGraph(emit->m_pMBirthRateGraph->m_Graph, 1, *semit->m_Graphs[MBIRTHRATE]);
		}

		#ifdef USE_FLOAT_PARAM_TEXTURE
			const uint voffa = 3;
			const uint voffr = 0;
			const uint voffg = 1;
			const uint voffb = 2;
			const uint voffsize = 0;
			const uint voffmsize = 3;
		#else
			const uint voffa = 3;
			const uint voffr = 2;
			const uint voffg = 1;
			const uint voffb = 0;
			const uint voffsize = 2;
			const uint voffmsize = 3;
		#endif

		ParamTextureItem texRow[PARAM_TEXTURE_WIDTH * 4];

		FillTexRow(texRow + voffa, semit->m_Graphs[COLOR_A], semit->m_Constants[COLOR_A]);
		FillTexRow(texRow + voffr, semit->m_Graphs[COLOR_R], semit->m_Constants[COLOR_R]);
		FillTexRow(texRow + voffg, semit->m_Graphs[COLOR_G], semit->m_Constants[COLOR_G]);
		FillTexRow(texRow + voffb, semit->m_Graphs[COLOR_B], semit->m_Constants[COLOR_B]);
		emit->FillTexGraph(0, texRow);
		FillTexRow(texRow + voffa, semit->m_Graphs[MCOLOR_A], semit->m_Constants[MCOLOR_A]);
		FillTexRow(texRow + voffr, semit->m_Graphs[MCOLOR_R], semit->m_Constants[MCOLOR_R]);
		FillTexRow(texRow + voffg, semit->m_Graphs[MCOLOR_G], semit->m_Constants[MCOLOR_G]);
		FillTexRow(texRow + voffb, semit->m_Graphs[MCOLOR_B], semit->m_Constants[MCOLOR_B]);
		emit->FillTexGraph(1, texRow);
		FillTexRow(texRow + voffsize, semit->m_Graphs[SIZE], semit->m_Constants[SIZE]);
		FillTexRow(texRow + voffmsize, semit->m_Graphs[MSIZE], semit->m_Constants[MSIZE]);
		emit->FillTexGraph(2, texRow);

		obj->m_pEmitorDefs[n].Optimize();
		obj->m_iAllFlags |= m_pEmitorDefs[n].m_iFlags;
	}

	obj->m_fVelocityAffection = m_fVelocityAffection;


	Vector3 boxRad = bbox.GetSize() * 0.5f;
	float rad = (boxRad + bbox.GetCenter()).Length();
	obj->SetBoundingBox(bbox, rad);

	IL_EXCHANGE_ADD(MemStats::m_iParticles, obj->GetSize());
}

//---------------------------------------------------------------------------
bool ParticleEffect::Load(CInputStream* instream)
{
	LoadParticleEffectResource res(this);

	if(res.Load(instream))
	{
		res.Apply();
		return true;
	}
	return false;
}

//---------------------------------------------------------------------------
void ParticleSystem::RenderBatch(Pipeline* pipeline, uint from, uint to, uint type, uint sort, uint verts, uint prims, Material* lmaterial, const BillboardSort& billsort)
{
	#ifdef ENF_RENDERQUADS
		MeshItem* mi = pipeline->BeginMesh((type == PT_POINT) ? PT_POINTLIST : PT_QUADLIST, sort, lmaterial);
	#else
		MeshItem* mi = pipeline->BeginMesh((type == PT_POINT) ? PT_POINTLIST : PT_TRIANGLELIST, sort, lmaterial);
	#endif

	if(mi)
	{
		if(type == PT_POINT)
		{
			ubyte* ptr = (ubyte*) m_pNewPointSpriteVB[m_iVB]->BeginAlloc(verts);
			ubyte* endptr = ptr + verts * sizeof(PSpriteVert);

			for(uint e = from; e < to; e++)
			{
				ptr = (ubyte*)XRenderPS(ptr, (ParticleEffectInstance*)m_pEffectsToRender[e].m_pEntity->GetInstance(), m_pEffectsToRender[e].m_iEmitor, billsort);
			}
			enf_assert(endptr - ptr == 0);

			mi->m_pVB[0] = m_pNewPointSpriteVB[m_iVB];
			mi->m_iStride[0] = sizeof(PSpriteVert);
			mi->m_iFirstVertex = m_pNewPointSpriteVB[m_iVB]->EndAlloc();
			mi->m_pIB = NULL;
		}
		else if (type == PT_SPRITE)
		{
			ubyte* ptr = (ubyte*) m_pNewSpriteVB[m_iVB]->BeginAlloc(verts*sizeof(SpriteVert));
			ubyte* endptr = ptr + verts * sizeof(SpriteVert);

			for(uint e = from; e < to; e++)
			{
				ptr = (ubyte*)XRenderSprites(ptr, (ParticleEffectInstance*)m_pEffectsToRender[e].m_pEntity->GetInstance(), m_pEffectsToRender[e].m_iEmitor, billsort);
			}
			enf_assert(endptr - ptr == 0);
			
			mi->m_pVB[1] = (VertexBuffer*) m_pNewSpriteVB[m_iVB]->EndAlloc();

			mi->m_pVB[0] = m_pNewSpriteVB[m_iVB];
			mi->m_iStride[0] = sizeof(SpriteVert);
			mi->m_iFirstVertex = 0;

			mi->m_iNumInstances = prims/PARTICLE_SPRITE_INSTPRIMITIVES;
			verts = PARTICLE_SPRITE_VBVERTS;
			prims = PARTICLE_SPRITE_INSTPRIMITIVES;
		}
		else // STREAKS
		{
			ubyte* ptr = (ubyte*) m_pNewStreakVB[m_iVB]->BeginAlloc(verts*sizeof(StreakVert));
			ubyte* endptr = ptr + verts * sizeof(StreakVert);

			for(uint e = from; e < to; e++)
			{
				ptr = (ubyte*)XRenderStreaks(ptr, (ParticleEffectInstance*)m_pEffectsToRender[e].m_pEntity->GetInstance(), m_pEffectsToRender[e].m_iEmitor, billsort);
			}
			enf_assert(endptr - ptr == 0);

			mi->m_pVB[1] = (VertexBuffer*) m_pNewStreakVB[m_iVB]->EndAlloc();

			mi->m_iFirstVertex = 0;
			mi->m_pVB[0] = m_pNewStreakVB[m_iVB];
			mi->m_iStride[0] = sizeof(StreakVert);


			mi->m_iNumInstances = prims/PARTICLE_STREAK_INSTPRIMITIVES;
			verts = PARTICLE_STREAK_VBVERTS;
			prims = PARTICLE_STREAK_INSTPRIMITIVES;
		}

		mi->m_iNumVertices = verts;
		mi->m_iPrimitiveCount = prims;
		mi->m_iFirstIndex = 0;
		mi->m_pIB = NULL;

		//get texture 
		uint emitor = m_pEffectsToRender[from].m_iEmitor;
		const ParticleEffectInstance* pi = (const ParticleEffectInstance*) m_pEffectsToRender[from].m_pEntity->GetInstance();
		const EmitorDef* def = &pi->GetVObject()->m_pEmitorDefs[emitor];
		mi->m_pUserData = (void*) def;

		pipeline->InsertToAmbientSet(mi);
		pipeline->EndMesh();

		ENF_ADD_COUNTER(PROFILE_PART_BATCHES, 1);
	}
}

//---------------------------------------------------------------------------
void ParticleSystem::RenderParticles(Pipeline* pipeline)
{
	if(m_pEffectsToRender.IsEmpty())
		return;

	QuickSort<EmitorItem>(m_pEffectsToRender.GetCardinality(), m_pEffectsToRender.GetArray());

	Material* material = NULL;
	ModelContext* mc = NULL;
	//HACK: Particle effect don't accept per entity parameters. It's too expensive
	//TODO: what to do with lights?
	if(m_iAllocIndex != INDEX_NOT_FOUND && (mc = pipeline->BeginObject(m_iAllocIndex, m_pEffectsToRender[0].m_pEntity, 0.0f, NULL, MCF_IDENTWORLD, 0)) != NULL)
	{
		ENF_ADD_COUNTER(PROFILE_PART_EMITTERS, m_pEffectsToRender.GetCardinality());

		const Matrix43& view = pipeline->GetCameraMatrix();

		BillboardSort billsort;
		Vector4 billboard[2];
		billsort.m_vBillboard1 = view[1];
		billsort.m_vBillboard2 = view[2];
		billsort.m_vBillboard1.z *= -1.0f;
		billsort.m_vBillboard2.z *= -1.0f;

		const Matrix44& mat = GetMaterialSystem()->GetMatrix(MaterialSystem::MV_VIEW_MATRIX);
		billsort.m_vSort = Vector3(mat._13, mat._23, mat._33);
		billsort.m_fSortZ = mat._43;

		int maxvertsspr = PARTICLE_MAX_SPRITES;
		int maxvertsstr = PARTICLE_MAX_STREAKS;
		int maxvertspoi = PARTICLE_POINT_VB_SIZE;

		int maxverts = 0;
		uint verts = 0;
		uint prims = 0;
		uint lemitor = 0;
		uint lsortval = -1;
		Material *lmaterial = NULL;
		PartType ltype;

		bool disablebatching = false;

		//2 highest bits and 19 lowest bits zero due to adding to material sortid in beginmesh
		const uint distand = 0x3FF80000;

		for(uint n = 0; n < m_pEffectsToRender.GetCardinality(); n++)
		{
			ParticleEffectInstance *pa = (ParticleEffectInstance *)m_pEffectsToRender[n].m_pEntity->GetInstance();
#ifdef _DEBUG
			pa->m_iLocked = 0;
#endif
			uint emitor = m_pEffectsToRender[n].m_iEmitor;

			const EmitorDef* def = &pa->GetVObject()->m_pEmitorDefs[emitor];
			const PEmitor* emit = pa->m_pEmitors[emitor];

/*
LogF(LL_NORMAL, "%s (%s), dist %d, emi %d, mat sort %d, mat part sort ", 
	def->m_strName.cstr(), 
	def->m_pMaterial->GetName().cstr(),
	m_pEffectsToRender[n].m_iSortValue >> 19,
	(m_pEffectsToRender[n].m_iSortValue >> 15) & 0xF,
	def->m_pMaterial->GetSortID()
//	(m_pEffectsToRender[n].m_iSortValue >> 15) << Pipeline::TRANS_CUSTOM_SORT_SHIFT
	);
*/			
			uint addprims, addverts, act = emit->m_iNumActiveParticles;

			PartType type = PType(def);
			switch(type)
			{
			case PT_SPRITE:
				{
					addverts = act;
					addprims = act*PARTICLE_SPRITE_INSTPRIMITIVES;
					maxvertsspr -= (int) addverts;
					maxverts = maxvertsspr;
				}
				break;

			case PT_POINT:
				addverts = act;
				addprims = act;
				maxvertspoi -= (int) addverts;
				maxverts = maxvertspoi;
				break;

			case PT_STREAK:
				addverts = act;
				addprims = act * PARTICLE_STREAK_INSTPRIMITIVES;
				maxvertsstr -= (int) addverts;
				maxverts = maxvertsstr;
				break;
			}

			//limit of VB overstepped
			if (maxverts < 0)
			{
				lsortval = 0;
				prims = verts = 0;
				disablebatching = false;
				continue;
			}

			if(disablebatching || m_pEffectsToRender[n].m_iSortValue != lsortval || (int) (verts + addverts) > maxverts)
			{
				//break
				if(verts || disablebatching)
				{
					//uint dist = lsortval >> 19;
					//uint nume = lsortval >> 15) & 0xF;

					//26 bit .. 13 bit are free
					uint sortval = (lsortval >> 15) << Pipeline::TRANSP_CUSTOM_SORT_SHIFT;

					RenderBatch(pipeline, lemitor, n, ltype, sortval, verts, prims, lmaterial, billsort);

					prims = verts = 0;
				}
				lmaterial = def->m_pMaterial;
				lsortval = m_pEffectsToRender[n].m_iSortValue;
				ltype = type;
				lemitor = n;
				disablebatching =  emit->IsBatchingDisabled();
			}

			verts += addverts;
			prims += addprims;
		}

		if(verts && maxverts > (int) verts)
		{
			uint sortval = (lsortval >> 15) << Pipeline::TRANSP_CUSTOM_SORT_SHIFT;

			RenderBatch(pipeline, lemitor, m_pEffectsToRender.GetCardinality(), ltype, sortval, verts, prims, lmaterial, billsort);
		}

		ENF_ADD_COUNTER(PROFILE_PART_POINTS, maxvertspoi);
		ENF_ADD_COUNTER(PROFILE_PART_SPRITES, maxvertsspr);
		ENF_ADD_COUNTER(PROFILE_PART_STREAKS, maxvertsstr);

		pipeline->EndObject();
	}
	else
	{
#ifdef _DEBUG
		for(uint n = 0; n < m_pEffectsToRender.GetCardinality(); n++)
		{
			ParticleEffectInstance *pa = (ParticleEffectInstance *)m_pEffectsToRender[n].m_pEntity->GetInstance();
			pa->m_iLocked = 0;
		}
#endif
	}

	m_iAllocIndex = INDEX_NOT_FOUND;
}


//-----------------------------------------------------------------------------
ENF_INLINE void EmitorTextureGraph::TextureContainer::AllocTexture()
{
	m_pTexture = g_pRender->CreateTextureWithFormat("EmitorParams", PARAM_TEXTURE_WIDTH, PARAM_TEXTURE_SIZE, PARAM_TEXTURE_FMT, TEXTR_LINEAR|TEXTR_NOMIPMAP|TEXTR_NORESIZE, MAP_UNKNOWN);
}


//-----------------------------------------------------------------------------
EmitorTextureGraph::TextureContainer::TextureContainer()
{
	AllocTexture(); 
	enf_assert(m_pTexture);

	for (uint i = 0; i < MAXINDICES; i++)
	{
		m_EmptyIndices.Insert(MAXINDICES - 1 - i);
	}
}



//-----------------------------------------------------------------------------
ENF_INLINE void* EmitorTextureGraph::TextureContainer::LockRow(uint index, uint ioffs)
{
	enf_assert(index < MAXINDICES);

	enf_assert(m_pTexture);
	enf_assert(index != INDEX_NOT_FOUND && index < MAXINDICES);

	uint top = index * EMIT_ROWS + ioffs;
	RRect lockrect(0, top, PARAM_TEXTURE_WIDTH, top + 1);

	LockedImageRect rect;
	bool ret = m_pTexture->Lock(0, &lockrect, &rect, NULL);

	if (!ret)
	{
		return NULL;
	}

	enf_assert(rect.pBits);
	return rect.pBits;
}


//-----------------------------------------------------------------------------
ENF_INLINE void EmitorTextureGraph::TextureContainer::Unlock()
{
	enf_assert(m_pTexture);

	m_pTexture->Unlock(0);
}


//-----------------------------------------------------------------------------
ENF_INLINE uint EmitorTextureGraph::GetEmptyIndex(TexGraphIndex& index)
{
	//find free index
	for (uint i = 0; i < m_Textures.GetCardinality(); i++)
	{
		if (m_Textures[i]->m_EmptyIndices.IsEmpty())
			continue;

		if (!m_Textures[i]->m_pTexture)
		{
			m_Textures[i]->AllocTexture();
		}

		index = m_Textures[i]->m_EmptyIndices.Pop();
		return i;
	}

	//noone free, create new container
	uint i = m_Textures.Insert(ENF_NEW TextureContainer);
	index = m_Textures[i]->m_EmptyIndices.Pop();

	return i;
}


//-----------------------------------------------------------------------------
ENF_INLINE void EmitorTextureGraph::ReleaseIndex(TexGraphIndex& index)
{
	if (index == TEXGRAPH_BAD_INDEX)
		return;

	uint texindex = UnpackIndex(index);

	enf_assert(m_Textures[texindex]->m_EmptyIndices.GetIndexOf(index) == INDEX_NOT_FOUND);
	m_Textures[texindex]->m_EmptyIndices.Insert(index);

	index = TEXGRAPH_BAD_INDEX;

	if (m_Textures[texindex]->m_EmptyIndices.GetCardinality() == MAXINDICES)
	{
		m_Textures[texindex]->m_pTexture.Release();
	}
}



//-----------------------------------------------------------------------------
TexGraphIndex EmitorTextureGraph::FillData(TexGraphIndex index, uint ioffs, const void* row)
{
	//unpack indices
	uint texindex = INDEX_NOT_FOUND;
	if (index == TEXGRAPH_BAD_INDEX)
	{
		texindex = GetEmptyIndex(index);

		if (index == TEXGRAPH_BAD_INDEX)
		{
			return TEXGRAPH_BAD_INDEX;
		}
	}
	else
	{
		texindex = UnpackIndex(index);		
	}

	uchar *data = (uchar*) m_Textures[texindex]->LockRow(index, ioffs); 
	if(data)
	{
		memcpy(data, row, sizeof(ParamTextureItem)*PARAM_TEXTURE_WIDTH*4);

		m_Textures[texindex]->Unlock();
	}

	return PackIndex(texindex, index);
}

//-----------------------------------------------------------------------------
void EmitorDef::FillTexGraph(uint ioffs, const ParamTextureItem* data)
{
	TexGraphIndex index = g_pParticleSystem->GetTextureGraph()->FillData(m_iTexIndex, ioffs, data );

	if (index != TEXGRAPH_BAD_INDEX && index != m_iTexIndex)
	{
		enf_assert(m_iTexIndex == TEXGRAPH_BAD_INDEX || (m_iTexIndex != TEXGRAPH_BAD_INDEX && index == m_iTexIndex));
		m_iTexIndex  = index;
		m_fGPUOffset = g_pParticleSystem->GetTextureGraph()->GetGPUIndex(index);
	}
}

//-----------------------------------------------------------------------------
EmitorTextureGraph::~EmitorTextureGraph()
{
	for (uint i = 0; i < m_Textures.GetCardinality(); i++)
	{
		SAFE_DELETE(m_Textures[i]);
	}

	m_Textures.Clear();
}


//-----------------------------------------------------------------------------
PUBLIC_API ParticleSystem* CreateParticleSystem()
{
	return ENF_NEW ParticleSystem();
}

}

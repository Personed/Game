//-----------------------------------------------------------------------------
// File: enf_vertexcache.cpp
//
// Desc: Dynamic VB/IB
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "enf_main.h"
#include "render/enf_vertexcache.h"
#include "render/enf_rendmain.h"
#include "render/enf_renderresources.h"
#include "system/enf_profiler.h"

namespace enf
{
//-----------------------------------------------------------------------------
bool VCache::IsValidEntry(const VCacheEntry& entry) const
{
	//the same spin frame
	if(m_pVB[m_iActive]->GetID() == entry.m_iID)
		return true;

	//if it's the next one, check if we were overwritten
	if((entry.m_iID + 1) == m_pVB[m_iActive]->GetID())
	{
		if(entry.m_iVertexOffset > m_iVertTopPos[m_iActive])
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
void VCache::Advance()
{
	m_iActive++;
	if(m_iActive >= m_iNum)
		m_iActive = 0;

	m_pIB[m_iActive]->Reset();

	//it's worthless on xbox

	m_pVB[m_iActive]->Reset();
}

//-----------------------------------------------------------------------------
VCache::VCache()
{
	m_iVertTopPos[0] = 0;
	m_iVertSize = 0;
	m_iNum = 0;
	m_pVB[0] = NULL;
	m_pIB[0] = NULL;
	m_iActive = 0;
}

//-----------------------------------------------------------------------------
VCache::~VCache()
{
	Free();
}

//-----------------------------------------------------------------------------
void VCache::Free()
{
	for(uint n = 0; n < m_iNum; n++)
	{
		SAFE_RELEASE(m_pVB[n]);
		SAFE_RELEASE(m_pIB[n]);
	}
	m_iNum = 0;
}

//-----------------------------------------------------------------------------
bool VCache::Init(uint vbuffsize, uint ibuffsize, uint count)
{
	//invalidate old entries
	for(uint n = 0; n < m_iNum; n++)
	{
		SAFE_RELEASE(m_pVB[n]);
	}

	//alloc vertexbuffers
	for(uint n = 0; n < count; n++)
	{
		m_pVB[n] = g_pRender->CreateVertexBuffer(RES_USAGE_WRITEONLY|RES_USAGE_DYNAMIC, vbuffsize, 1, "VCacheVB");

		if(ibuffsize)
			m_pIB[n] = g_pRender->CreateIndexBuffer(IBFMT_INDEX16, RES_USAGE_WRITEONLY|RES_USAGE_DYNAMIC, ibuffsize, "VCacheIB");
		else
			m_pIB[n] = NULL;

		m_iVertTopPos[n] = 0;
	}

	m_iVertSize = vbuffsize;
	m_iNum = count;

	return true;
}

//-----------------------------------------------------------------------
void VCache::EndAlloc()
{
	m_pVB[m_iActive]->Unlock();
}

//-----------------------------------------------------------------------
void* VCache::Alloc(uint flags, uint verts, uint stride, VCacheEntry* entry)
{
	int needsize = verts * stride;
	uint pos;

	uint usabletop = enf_align(m_iVertTopPos[m_iActive], stride);
	int size = m_iVertSize - usabletop;

	uint lock;

	if(needsize > size)
	{
//		lock = RES_LOCK_DISCARD;
		lock = RES_LOCK_NOOVERWRITE;
		m_iVertTopPos[m_iActive] = needsize;
		m_pVB[m_iActive]->IncrementID();

		pos = 0;
	}
	else
	{
		lock = RES_LOCK_NOOVERWRITE;
		m_iVertTopPos[m_iActive] = (usabletop + needsize);
		pos = usabletop;
	}

	if(entry)
	{
		entry->m_iVertexOffset = pos;
		entry->m_iID = m_pVB[m_iActive]->GetID();
	}

	return m_pVB[m_iActive]->Lock(pos, needsize, lock);
}
}
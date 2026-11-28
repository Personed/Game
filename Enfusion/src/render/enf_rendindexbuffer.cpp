//-----------------------------------------------------------------------------
// File: enf_rendindexbuffer.cpp
//
// Desc: Dynamic index Buffer class & IB abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "enf_main.h"
#include "render/enf_irendmain.h"
#include "render/enf_renderresources.h"
#include "system/enf_profiler.h"

namespace enf
{
//-----------------------------------------------------------------------
IndexBuffer *IRenderer::CreateIndexBuffer(IndexBufferFormat fmt, uint usage, uint len, CStr name)
{
	enf_rlsassert(CheckThread());
	IndexBuffer *ib = ENF_NEW IndexBuffer(this, fmt, usage, len);

	#ifdef ENF_DEVELOPER
		ib->m_strName = name;
	#endif
	AddResource(ib);

	return ib;
}

//-----------------------------------------------------------------------
ERESULT IndexBuffer::Allocate()
{
	size_t memSize = GetMemorySize();
	Free();

	#if defined(ENF_OGL)
		GLCALL(glGenBuffers(1, &m_iBuffer));
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iBuffer));

		GLenum usage = (m_iUsage & RES_USAGE_DYNAMIC) ? GL_STREAM_DRAW : GL_STATIC_DRAW;
		GLCALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, memSize, NULL, usage));
		GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);
		return EE_OK;
	#endif

	return EE_ERROR;
}

//-----------------------------------------------------------------------
void IndexBuffer::Free()
{
	enf_rlsassert(GetDevice()->CheckThread());

	#if defined(ENF_OGL)
		if(m_iBuffer)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);

			GLCALL(glDeleteBuffers(1, &m_iBuffer));
			m_iBuffer = 0;
		}
	#endif

	MemStats::m_iTotalIndices -= GetMemorySize();
}

//-----------------------------------------------------------------------
IndexBuffer::IndexBuffer(IRenderer* dev, IndexBufferFormat fmt, uint usage, uint len): RenderBuffer(dev)
{
	#if defined(ENF_OGL)
		m_Format = fmt;
	#endif


	m_iUsage = usage;
	m_iLength = len * ((fmt == IBFMT_INDEX16) ? 2 : 4);
	Allocate();
}

//-----------------------------------------------------------------------
uint IndexBuffer::EndAlloc()
{
	enf_rlsassert(GetDevice()->CheckThread());

	#if defined(ENF_OGL)
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iBuffer));
		GLCALL(glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
		GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);
	#endif

	return m_iDBOffset;
}

//-----------------------------------------------------------------------
bool IndexBuffer::IsAllocated(uint id, uint offset) const
{
	if(id != GetID())
		return false;
	if(offset > m_iDBOffset)
		return false;
	return true;
}

//-----------------------------------------------------------------------
void* IndexBuffer::BeginAlloc(uint numind, uint* id)
{
	enf_rlsassert(GetDevice()->CheckThread());
	void* di;
	uint usage;
	uint stride;

	uint offset = numind;

	if(GetFormat() == IBFMT_INDEX16)
	{
		stride = 2;

		//force align
		if(offset & 1)
			offset += 1;
	}
	else
		stride = 4;

	uint size = GetSize();
	enf_assert(offset <= size);

	if((m_iOffset + numind) < size)
	{
		usage = RES_LOCK_NOOVERWRITE;
		m_iDBOffset = m_iOffset;
		m_iOffset += offset;
	}
	else
	{
		usage = RES_LOCK_DISCARD;
		m_iDBOffset = 0;
		m_iOffset = offset;
		IncrementID();
	}

	ENF_PROFILE(PROFILE_LOCK_IB);
	#if defined(ENF_OGL)
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iBuffer));
		GLCALL(di = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY));
		GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);
		return ((char*)di + (m_iDBOffset * stride));
	#endif

	return NULL;
}

//-----------------------------------------------------------------------
void*	IndexBuffer::Lock(uint from, uint num, uint flags)
{
	enf_rlsassert(GetDevice()->CheckThread());
	uint shift = (GetFormat() == IBFMT_INDEX16) ? 1 : 2;
	enf_assert((from + num) <= GetSize());

	ENF_PROFILE(PROFILE_LOCK_IB);

	void *ptr;

	#if defined(ENF_OGL)
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iBuffer));
		GLuint access = 0;
		if(flags & RES_LOCK_READONLY)
			access = GL_READ_ONLY;
		else if(flags & RES_LOCK_WRITEONLY)
			access = GL_WRITE_ONLY;
		else
			access = GL_READ_WRITE;

		GLCALL(ptr = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, access));
		GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);
		return ((char*)ptr + (from << shift));
	#endif
	
	return NULL;
}

//-----------------------------------------------------------------------
void IndexBuffer::Unlock()
{
	enf_rlsassert(GetDevice()->CheckThread());
	#if defined(ENF_OGL)
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iBuffer));
		GLCALL(glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
		GetDevice()->DirtyMask(Renderer::GLCM_INDEXBUFFER);
	#endif
}
}
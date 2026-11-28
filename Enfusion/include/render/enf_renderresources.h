//-----------------------------------------------------------------------------
// File: render/enf_rendresources.h
//
// Desc: Main render class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_RENDRESOURCES_H
#define ENF_RENDRESOURCES_H

#include "enf_config.h"
#include "common/enf_types.h"
#include "render/enf_rendertypes.h"
#include "common/enf_defineset.h"
#include "engine/enf_item.h"
#include "Render/enf_renderenums.h"
#ifdef ENF_OGL
	#include <gl/glew.h>
#endif

namespace enf
{
/** @addtogroup Renderer
@{*/

class IRenderer;
class CInputStream;

/*!
Master record for device related resources.
Consoles don't need to restore device, so we can save couple of bytes
*/
class PUBLIC_API DeviceResource: public BaseItem, public RefCountObject
{
	friend class IRenderer;
	friend class Renderer;

private:
#if defined(ENF_DEVELOPER)
	DeviceResource*			m_pNext;
#endif
	IRenderer*					m_pDevice;
	uint							m_iID;

protected:
	virtual void Swap(DeviceResource* other)
	{
		enf_swap(m_iID, other->m_iID);
	}


	/*!
	Virtual method. Must be implemented in all inherited classes. It has to allocate
	directx resource. All necessary parameters should be available in the class too.
	*/
	virtual ERESULT Allocate();
	
	virtual void Free()
	{
	}

	DeviceResource(IRenderer *dev):
#if defined(ENF_DEVELOPER)
		m_pNext(NULL),
#endif
		m_pDevice(dev),
		m_iID(1)
	{
	}

public:
	ENF_DECLARE_ITEM(DeviceResource, BaseItem);
	ENF_DECLARE_SMALLOBJECT(DeviceResource);

	ENF_INLINE IRenderer* GetDevice() const
	{
		return m_pDevice;
	}

	int Release();

	/*!
	Increments unique identifier. It invalidates all dynamically
	allocated data.
	*/
	ENF_INLINE void IncrementID()
	{
		m_iID++;
	}

	/*!
	Gets current unique identifier.
	\return Unique identifier of dynamically allocated data
	*/
	ENF_INLINE uint GetID() const
	{
		return m_iID;
	}

	virtual size_t GetMemorySize() const=0;
	virtual CStr GetName() const
	{
		return NULL;
	}
	virtual ~DeviceResource()
	{
	}
};

//-----------------------------------------------------------------------
class PUBLIC_API RenderQuery: public DeviceResource
{
	friend class Renderer;
	friend class IRenderer;

private:
#if defined(ENF_OGL)
	GLuint						m_iQuery;
#endif

	uint							m_iQueryType;

	RenderQuery(IRenderer *dev, uint type);

	ERESULT Allocate();
	void Free();
	bool	m_bIssued;

public:
	ENF_DECLARE_ITEM(RenderQuery, DeviceResource);

	void Issue(QueryCommand cmd);
	ENF_INLINE bool IsIssued() const
	{
		return m_bIssued;
	}
	size_t GetMemorySize() const
	{
		return 0;
	}

	ERESULT GetData(void* data, uint size, uint flags);
};

//-----------------------------------------------------------------------
class PUBLIC_API RenderBuffer: public DeviceResource
{
	friend class Renderer;
	friend class IRenderer;

protected:
	uint				m_iOffset;
	uint				m_iDBOffset;
	uint				m_iUsage;
#ifdef ENF_OGL
	GLuint			m_iBuffer;
#endif
#ifdef ENF_DEVELOPER
	CStr				m_strName;
#endif
	RenderBuffer(IRenderer* dev);
	~RenderBuffer();

public:
	ENF_DECLARE_ITEM(RenderBuffer, DeviceResource);

	/*!
	See IndexBuffer::BeginAlloc
	\param offset Offset from begining of buffer
	\param id ID you've got from IndexBuffer::BeginAlloc
	*/
	ENF_INLINE bool IsValid(uint offset, uint id)
	{
		return (id == GetID() && offset < m_iOffset);
	}

	virtual CStr GetName() const
	{
#ifdef ENF_DEVELOPER
		return m_strName;
#else
		return NULL;
#endif
	}
};

//-----------------------------------------------------------------------
class PUBLIC_API IndexBuffer: public RenderBuffer
{
	friend class Renderer;
	friend class IRenderer;

private:
#if defined(ENF_OGL)
#endif

	uint							m_iLength;
	IndexBufferFormat			m_Format;

	IndexBuffer(IRenderer *dev, IndexBufferFormat fmt, uint flags, uint len);

	ERESULT Allocate();
	void Free();

public:
	ENF_DECLARE_ITEM(IndexBuffer, DeviceResource);

	ENF_INLINE IndexBufferFormat GetFormat() const
	{
		return m_Format;
	}

	/*!
	Returns size of buffer in indices
	*/
	ENF_INLINE size_t GetSize() const
	{
		return m_iLength >> (GetFormat() == IBFMT_INDEX16 ? 1 : 2);
	}

	size_t GetMemorySize() const
	{
		return m_iLength;
	}

	/*!
	Locks content of IndexBuffer.
	\param from Offset from beginning of buffer
	\param numindices Number of indices to be locked
	\param flags See enf::LockFlags
	\return Pointer to content, or NULL
	*/
	void* Lock(uint from, uint numindices, uint flags);

	//!Unlocks content
	void Unlock();

	/*!
	Copies data to IndexBuffer.
	\param ptr Pointer to source data
	\param from	First index to be locked
	\param numVerts Number of indices to be locked
	\param flags See enf::LockFlags
	*/
	void CopyData(void* ptr, uint from, uint numIndices, uint flags);

	/*!
	Allocates part of buffer as from circular buffer. 
	Allowed only for buffers created with RES_USAGE_DYNAMIC
	\param numindices Number of indices to be allocated
	\param id Pointer to spin-counter. With this number and IndexBuffer::IsValid()
	you can check whether someone overwrote your data
	\return Pointer to content, or NULL
	*/

	void* BeginAlloc(uint numindices, uint* id = NULL);
	/*!
	Unlocks allocated content.
	\return Offset from beginning of buffer
	*/
	uint EndAlloc();

	bool IsAllocated(uint id, uint offset) const;

	/*!
	See IndexBuffer::BeginAlloc
	Resets dynamic pointer to the beginning of buffer
	*/
	ENF_INLINE void Reset()
	{
		//force reset when Start()
		m_iOffset = GetSize();
	}
};

//-----------------------------------------------------------------------
class PUBLIC_API VertexBuffer: public RenderBuffer
{
	friend class Renderer;
	friend class IRenderer;

private:

	uint		m_iStride;
	uint		m_iLength;
#if defined(ENF_OGL)
	GLuint						m_VB;
#endif

	VertexBuffer(IRenderer *dev, uint flags, uint len, uint stride);

	ERESULT Allocate();
	void Free();

public:
	ENF_DECLARE_ITEM(VertexBuffer, DeviceResource);

	ENF_INLINE size_t GetSize() const
	{
		return m_iLength * m_iStride;
	}

	ENF_INLINE uint GetNumVerts() const
	{
		return m_iLength;
	}

	size_t GetMemorySize() const
	{
		return GetSize();
	}

	/*!
	Locks content of static VertexBuffer.
	\param from
	First vertex to be locked
	\param numverts
	Number of vertexes to be locked
	\param flags
	See enf::LockFlags
	\return
	Pointer to locked buffer
	*/
	void*	Lock(uint from, uint numverts, uint flags);

	/*!
	Unlocks VertexBuffer
	*/
	void	Unlock();

	/*!
	Copies data to VertexBuffer.
	\param ptr Pointer to source data
	\param from	First vertex to be locked
	\param numVerts Number of vertexes to be locked
	\param flags See enf::LockFlags
	*/
	void CopyData(void* ptr, uint from, uint numVerts, uint flags);

	/*!
	Allocates and locks defined number of verts. It is suitable for dynamic VertexBuffer,
	where you alloc just temporary buffers.
	\param numverts
	Number of vertexes to be allocated and locked
	\param id
	Unique identifier of locked content. These data can be anywhere lost because of
	lost device, or because of overwrite. Always ask if your data are valid by VertexBuffer::IsValid()
	\return
	Pointer to locked buffer of NULL when there is no space
	*/
	void*	BeginAlloc(uint numverts, uint* id = NULL);

	/*!
	Unlocks allocated buffer
	\return Offset of allocated buffer (first vertex)
	*/
	uint	EndAlloc();

	/*!
	Enforces reset of dynamic buffer. The next BeginAlloc will wrap over
	the buffer and invalidates all old data.
	*/
	void	Reset()
	{
		//force reset when Start()
		m_iOffset = GetNumVerts();
	}
};

//-----------------------------------------------------------------------

#define PSDEF(prf) enf::PRF_PS30
#define VSDEF(prf) enf::PRF_VS30

ENF_INLINEFUNC bool IsPixelShader(ProfileType type)
{
	return type >= PRF_PS20;
}
ENF_INLINEFUNC bool IsVertexShader(ProfileType type)
{
	return type < PRF_PS20;
}

//-----------------------------------------------------------------------
class PUBLIC_API ShaderDef
{
protected:
	const char* m_strName;
	ProfileType	m_Profile;
	DefineSet	m_Defines;

public:
	ShaderDef(CStr name, ProfileType profile): m_strName(NULL), m_Profile(profile)
	{
		if(!name.IsEmpty()) SetName(name);
	}
	ShaderDef(): m_strName(NULL), m_Profile(PRF_NONE)
	{
	}

	ProfileType GetProfile() const
	{
		return m_Profile;
	}

	void SetName(CStr name);
	CStr GetName() const { return m_strName; }
	const DefineSet& GetDefines() const { return m_Defines; }
	DefineSet& GetDefines() { return m_Defines; }
	void Add(const Define& def)
	{
		m_Defines.Insert(def);
	}
};

//-----------------------------------------------------------------------
class PUBLIC_API RenderShader: public DeviceResource, public ShaderDef
{
protected:
	friend class Renderer;
	friend class IRenderer;

	void*	m_pShader;

public:
	ENF_DECLARE_ITEM(RenderShader, DeviceResource);

	RenderShader(IRenderer* dev, const ShaderDef* def): DeviceResource(dev), ShaderDef(), m_pShader(NULL)
	{
		if(def)
			*(ShaderDef*)this = *def;
	}
	~RenderShader();
	size_t GetMemorySize() const
	{
		return 0;
	}
	CStr GetName() const
	{
		return ShaderDef::GetName();
	}
	virtual bool Load(CInputStream& stream)=0;
};

//-----------------------------------------------------------------------
class PUBLIC_API PixelShader: public RenderShader
{
	friend class Renderer;
	friend class IRenderer;

	void Free();

public:
	ENF_DECLARE_ITEM(PixelShader, RenderShader);
	PixelShader(IRenderer *dev, const ShaderDef* def);
	bool Load(CInputStream& stream);
};

//-----------------------------------------------------------------------
class PUBLIC_API VertexShader: public RenderShader
{
	friend class Renderer;
	friend class IRenderer;

	void Free();

public:
	ENF_DECLARE_ITEM(VertexShader, RenderShader);
	VertexShader(IRenderer *dev, const ShaderDef* def);
	bool Load(CInputStream& stream);
};

//-----------------------------------------------------------------------

//@}
}
#endif //ENF_RENDRESOURCES_H
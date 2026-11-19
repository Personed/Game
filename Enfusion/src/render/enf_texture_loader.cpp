//-----------------------------------------------------------------------------
// File: enf_texture_loader.cpp
//
// Desc: Texture loader
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "render/enf_irendmain.h"
#include "render/enf_texture.h"
//#include "script/enf_scriptapivars.h"
#include "resource/enf_imageloader.h"
#include "resource/enf_resourcemanager.h"
#include "system/enf_vfilesystem.h"
#include "common/enf_streams.h"

#if defined(ENF_DX9) 
	#include <d3dx9.h>
#endif

namespace enf
{


//-----------------------------------------------------------------------------
#ifdef ENF_DX9
class TextureImageData: public ImageData
{
protected:
	ComPtr<Texture> m_pTexture;
	uint						m_iFlags;
	CStr						m_strName;
	Renderer*				m_pRenderer;
	uint						m_iUsedMipmaps[6];

public:
	ENF_DECLARE_SMALLOBJECT(TextureImageData);
	ComPtr<Texture> GetTexture()
	{
		return m_pTexture;
	}

	size_t GetDataSize() const
	{
		//TODO: implement this!
		return 0;
	}

	void Free()
	{
		m_pTexture.Release();
	}
	ENF_INLINE TextureFormat GetFormat() const
	{
		return m_pTexture->GetFormat();
	}
	ENF_INLINE uint GetWidth() const
	{
		return m_pTexture->GetWidth();
	}
	ENF_INLINE uint GetHeight() const
	{
		return m_pTexture->GetHeight();
	}

	ENF_INLINE uint GetNumFaces() const
	{
		enf_assert(!m_pTexture.IsNull());
		return m_pTexture->IsCube() ? 6 : 1;
	}
	uint GetNumLevels() const
	{
		enf_assert(!m_pTexture.IsNull());
		return m_pTexture->GetNumLevels();
	}
	ERESULT Create(uint width, uint height, uint levels, TextureFormat format, uint flags, TextureUsage usage)
	{
		enf_assert(width != 0);
		enf_assert(height != 0);
		//enf_assert(levels != 0);
		enf_assert(format != TEXFMT_UNKNOWN);

		uint nfaces = (flags & TEXTR_CUBE) ? 6 : 1;
		for(uint f = 0; f < nfaces; f++)
		{
			m_iUsedMipmaps[f] = 0;
		}

		m_pTexture = m_pRenderer->CreateTextureWithFormat(m_strName, width, height, format, flags, usage);
		if(m_pTexture.IsNull())
			return EE_ERROR;

		return EE_OK;
	}

	bool Lock(uint face, uint level, LockedImageRect& info)
	{
		enf_assert(!m_pTexture.IsNull());
/*		enf_assert(face < m_iNumFaces);
		enf_assert(level < m_iLevels);

		if(face >= m_iNumFaces || level >= m_iLevels)
			return false;
*/
		m_iUsedMipmaps[face] |= (1 << level);

		D3DRESOURCETYPE resType = m_pTexture->GetD3DTexture()->GetType();
		if(resType == D3DRTYPE_TEXTURE)
		{
			IDirect3DTexture9* tex = (IDirect3DTexture9*)m_pTexture->GetD3DTexture();
			return (tex->LockRect(level, (D3DLOCKED_RECT*)&info, NULL, 0) == D3D_OK);
		}
		else if(resType == D3DRTYPE_CUBETEXTURE)
		{
			IDirect3DCubeTexture9* tex = (IDirect3DCubeTexture9*)m_pTexture->GetD3DTexture();
			return (tex->LockRect((D3DCUBEMAP_FACES)face, level, (D3DLOCKED_RECT*)&info, NULL, 0) == D3D_OK);
		}
		else if(resType == D3DRTYPE_VOLUMETEXTURE)
		{
		}
		return false;
	}

	void Unlock(uint face, uint level)
	{
		enf_assert(!m_pTexture.IsNull());
		enf_assert(face < 6);

		D3DRESOURCETYPE resType = m_pTexture->GetD3DTexture()->GetType();
		if(resType == D3DRTYPE_TEXTURE)
		{
			IDirect3DTexture9* tex = (IDirect3DTexture9*)m_pTexture->GetD3DTexture();
			tex->UnlockRect(level);
		}
		if(resType == D3DRTYPE_CUBETEXTURE)
		{
			IDirect3DCubeTexture9* tex = (IDirect3DCubeTexture9*)m_pTexture->GetD3DTexture();
			tex->UnlockRect((D3DCUBEMAP_FACES)face, level);
		}
		else if(resType == D3DRTYPE_VOLUMETEXTURE)
		{
		}
	}
	void Finnish()
	{
		enf_assert(!m_pTexture.IsNull());
		uint nlevels = m_pTexture->GetNumLevels();
		uint nfaces = (m_pTexture->GetD3DTexture()->GetType() == D3DRTYPE_CUBETEXTURE) ? 6 : 1;

		for(uint l = 0; l < nlevels; l++)
		{
			bool isSet = true;
			for(uint f = 0; f < nfaces; f++)
			{
				if((m_iUsedMipmaps[f] & (1 << l)) == 0)
				{
					isSet = false;
					break;
				}
			}

			//generate unset mipmaps
			if(!isSet)
			{
				if(l == 0)
				{
					enf_assert2(l != 0, "No data are set");
					return;
				}
				#ifndef ENF_DUMMYRENDER
					D3DXFilterTexture(m_pTexture->GetD3DTexture(), NULL, l - 1, D3DX_FILTER_BOX);
				#endif
				return;
			}
		}
	}

	TextureImageData(Renderer* rend, CStr name, uint flags)
	{
		m_pRenderer = rend;
		m_iFlags = flags;
		m_strName = name;
	}
	~TextureImageData()
	{
	}
};
#endif

//-----------------------------------------------------------------------------
Texture* IRenderer::CreateTextureFromImage(CStr name, CInputStream& instream, uint dwFlags)
{
ImageInfo imginfo;

Texture *tc = NULL;
ushort dwMipLevels;
TextureFormat fmt = ComputeFormat(dwFlags, &dwMipLevels);

	ERESULT hr = EE_ERROR;

	ZeroMem(&imginfo, sizeof(imginfo));

	#ifdef ENF_DX9
		TextureImageData imageData(this, name, dwFlags);
	#else
		MemoryImageData imageData;
	#endif

	hr = GetResources()->GetImageLoader()->Load(instream, imginfo, GetResources()->GetTextureDetail(), dwMipLevels, dwFlags, imageData);

	if(hr != EE_OK)
		return NULL;

	if(imginfo.MipLevels == 1)
		dwFlags |= TEXTR_NOMIPMAP;

	#ifdef ENF_DX9
		Texture* tex = imageData.GetTexture();
		if(tex == NULL)
			return NULL;

		tex->AddRef();
	#else
		uint width = imageData.GetWidth();
		uint height = imageData.GetHeight();
		fmt = imageData.GetFormat();

		Texture* tex;

		#ifdef ENF_PS3
			if ((dwFlags & TEXTR_VERTEXTEXTURE) && (fmt == TEXFMT_A8L8 || fmt == TEXFMT_A8 || fmt == TEXFMT_L8))
			{
				//clear mipmaps, not neccessary in PS3 VS
				dwFlags |= TEXTR_NOMIPMAP;
				tex = g_pRender->CreateTextureWithFormat(NULL, width, height, TEXFMT_R32F, dwFlags, MAP_DIFFUSE);
			}
			else
		#endif
			tex = g_pRender->CreateTextureWithFormat(name, width, height, fmt, dwFlags, MAP_DIFFUSE);

		if(tex == NULL)
			return NULL;
		tex->CopyData(imageData, dwFlags);
	#endif

  return tex;
}

}
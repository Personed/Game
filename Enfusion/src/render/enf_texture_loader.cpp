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


namespace enf
{


//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Texture* IRenderer::CreateTextureFromImage(CStr name, CInputStream& instream, uint dwFlags)
{
ImageInfo imginfo;

Texture *tc = NULL;
ushort dwMipLevels;
TextureFormat fmt = ComputeFormat(dwFlags, &dwMipLevels);

	ERESULT hr = EE_ERROR;

	ZeroMem(&imginfo, sizeof(imginfo));

		MemoryImageData imageData;

	hr = GetResources()->GetImageLoader()->Load(instream, imginfo, GetResources()->GetTextureDetail(), dwMipLevels, dwFlags, imageData);

	if(hr != EE_OK)
		return NULL;

	if(imginfo.MipLevels == 1)
		dwFlags |= TEXTR_NOMIPMAP;

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

  return tex;
}

}

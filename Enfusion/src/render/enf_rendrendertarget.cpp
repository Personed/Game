//-----------------------------------------------------------------------------
// File: enf_rendrendertarget.cpp
//
// Desc: Render target abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "render/enf_irendmain.h"
#include "render/enf_texture.h"

namespace enf
{

//-----------------------------------------------------------------------
RenderTarget::RenderTarget(IRenderer *dev, uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, uint edramoffset):
	Texture(dev, NULL, width, height, 0),
#if defined(ENF_OGL)
	m_iFrameBuffer(0),
	m_iDepthBuffer(0),
#endif
	m_DepthFormat(depthfmt),
	m_iUsage(0)
{
	m_Format = rtfmt;
}

//-----------------------------------------------------------------------
void RenderTarget::Free()
{
	enf_rlsassert(GetDevice()->CheckThread());
	MemStats::m_iRenderTargets -= GetMemorySize();

	#if defined(ENF_OGL)
		if(m_iDepthBuffer != 0)
		{
			glDeleteRenderbuffersEXT(1, &m_iDepthBuffer);
			m_iDepthBuffer = 0;
		}
		if(m_iTexture != 0)
		{
			glDeleteTextures(1, &m_iTexture);
			m_iTexture = 0;
		}
	#endif
}

//-----------------------------------------------------------------------
size_t RenderTarget::GetMemorySize() const
{
	size_t size = Texture::GetMemorySize();
	return size;
}

//-----------------------------------------------------------------------
ERESULT RenderTarget::Allocate()
{
	#if defined(ENF_OGL)

		GLuint renderedTexture;
		GLCALL(glGenTextures(1, &renderedTexture));
		GLCALL(glBindTexture(GL_TEXTURE_2D, renderedTexture));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
	
		GLenum dataFormat, dataType;
		GLint glfmt = TexFormatToGLFormatEx(m_Format, dataFormat, dataType, true);
		GLCALL(glTexImage2D(GL_TEXTURE_2D, 0, glfmt, m_iWidth, m_iHeight, 0, dataFormat, dataType, 0));

		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP));
		// Poor filtering. Needed !
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));

		if(m_DepthFormat != TEXFMT_UNKNOWN)
		{
			GLuint depthrenderbuffer;
			GLCALL(glGenRenderbuffers(1, &depthrenderbuffer));
			GLCALL(glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer));
			GLCALL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_iWidth, m_iHeight));
			m_iDepthBuffer = depthrenderbuffer;
		}

		m_iTexture = renderedTexture;
		// Set "renderedTexture" as our colour attachement #0
		//GLCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, renderedTexture, 0));

		//GL_CHECKFBO(GL_FRAMEBUFFER_EXT)

		
		GLint primaryBackBuffer;
		GLint primaryDepthBuffer;


	//	GLCALL(glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &primaryBackBuffer));
		//GLCALL(glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &primaryDepthBuffer));

		//GLCALL(glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK_RIGHT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &primaryBackBuffer));
		//GLCALL(glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &primaryDepthBuffer));
		//GLCALL(glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &primaryDepthBuffer));

		GLCALL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
		GLCALL(glBindTexture(GL_TEXTURE_2D, 0));

		GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES|Renderer::GLCM_SAMPLERFILTER|Renderer::GLCM_FBO);

	#endif

	MemStats::m_iRenderTargets += GetMemorySize();
	return EE_OK;
}

//-----------------------------------------------------------------------
RenderTarget* IRenderer::CreateRenderTarget(uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, FSAAType fsaa, uint usage, uint edramoffset, uint texFlags, CStr name)
{
	enf_rlsassert(CheckThread());

	RenderTarget *rt = ENF_NEW RenderTarget(this, width, height, rtfmt, depthfmt, edramoffset);
	rt->m_FSAAType = fsaa;
	rt->m_iUsage = usage | RES_USAGE_RENDERTARGET;
	rt->m_iFlags |= texFlags;
	#ifdef ENF_DEVELOPER
		rt->m_strName = name;
	#endif

	if(rt->Allocate() != EE_OK)
	{
		rt->Free();
		SAFE_DELETE(rt);
		return NULL;
	}

	AddResource(rt);

	return rt;
}

//-----------------------------------------------------------------------
void RenderTarget::Resolve(uint rt, const RRect* rect, const RPoint* point)
{
	enf_assert(rt < 4);

	#if defined(ENF_OGL)
	#endif
}





}
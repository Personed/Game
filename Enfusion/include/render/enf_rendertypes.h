//-----------------------------------------------------------------------------
// File: render/enf_rendertypes.h
//
// Desc: Main render class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_RENDERTYPES_H
#define ENF_RENDERTYPES_H

#include "enf_config.h"
#include "common/enf_types.h"
#include "engine/enf_event.h"
#include "Render/enf_renderenums.h"

//D3D types forward declaration

namespace enf
{
//Renderer types forward declaration
class Renderer;
class RenderTarget;
class RenderShader;
class ShaderDef;
class VertexShader;
class PixelShader;
class SwapChain;
class RenderQuery;
class DeviceResource;
class IndexBuffer;
class VertexBuffer;
typedef void* IVertexDeclaration;


typedef uint SamplerFilterStateHandle;
typedef uint SamplerAddressStateHandle;
typedef uint DepthStencilStateHandle;


struct LockedImageRect
{
	int		Pitch;
	void*	pBits;
};

/*!
Defines rectangle
*/
struct RRect
{
	uint Left, Top;
	uint Right, Bottom;

	RRect(): Left(0), Top(0), Right(0), Bottom(0) { }
	RRect(uint left, uint top, uint right, uint bottom): Left(left), Top(top), Right(right), Bottom(bottom) {}
};

/*!
Defines Point
*/
struct RPoint
{
	int	X, Y;
	RPoint(): X(0), Y(0) {}
	RPoint(int x, int y): X(x), Y(y) {}
};

/*!
Defines scissor region into which device renders
*/
struct RenderViewport
{
	uint	X, Y;
	uint	Width, Height;
	float MinZ, MaxZ;

	RenderViewport()
	{
		Init();
	}

	/*!
	initialize viewport to default values
	*/
	void Init()
	{
		X = 0;
		Y = 0;
		Width = 0;
		Height = 0;
		MinZ = 0.0f;
		MaxZ = 0.0f;
	}

	/*!
	create viewport
	\param x			start x in pixels
	\param y			start y in pixels
	\param width	width of viewport in pixels
	\param height	height of viewport in pixels
	\param minz		minimum z value
	\param maxz		maximum z value
	*/
	RenderViewport(int x, int y, int width, int height, float minz = 0.0f, float maxz = 1.0f): X(x), Y(y), Width(width), Height(height), MinZ(minz), MaxZ(maxz)
	{
		enf_assert(x >= 0 && y >= 0 && width >= 0 && height >= 0 && minz >= 0.0f && maxz <= 1.0f);
	}

	/*!
	create viewport from zero with default minz = 0, maxz = 1
	\param width	width of viewport in pixels
	\param height	height of viewport in pixels
	*/
	RenderViewport(int width, int height): X(0), Y(0), Width(width), Height(height), MinZ(0.0f), MaxZ(1.0f)
	{
		enf_assert(width >= 0 && height >= 0);
	}
};

struct LockedRect
{
	int		Pitch;
	void*	pBits;
};

/*!
Event used for reporting that everyone has to reload its
shaders effects, hlsl code, whatever.
*/
class PUBLIC_API ReloadShadersEvent: public Event
{
public:
	ENF_DECLARE_ITEM(ReloadShadersEvent, Event);
};


ENF_INLINEFUNC uint F2DW( float f ) 
{ 
	return *((uint*)&f); 
}

}

#endif //ENF_RENDERTYPES_H
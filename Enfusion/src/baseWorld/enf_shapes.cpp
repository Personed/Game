//# pragma optimize( "", off ) 

//-----------------------------------------------------------------------------
// File: enf_shapes.h
//
// Desc: Debug visualization library.
//
// Copyright (c) 2000-2010 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------
#include "BaseWorld/enf_shapes.h"
#include "common/enf_winding.h"
#include "common/enf_quicksort.h"

#include "common/enf_mathlib.h"
#include "El/Color/colorsFloat.hpp"
#include "lib/camera.hpp"
#include "lib/scene.hpp"
#include "es/Threads/multisync.hpp"

#ifndef ENF_SHAPES_DUMMYRENDER
	#include "lib/d3d9/postProcess.hpp"
	#include "lib/d3d9/engdd9.hpp"

	extern void ConvertMatrixTransposed(PASSMATRIXTYPE &mat, Matrix4Val src);
	extern void ConvertMatrixTransposed3(MatrixFloat34 &mat, Matrix4Val src);
	extern void ConvertMatrix3(MatrixFloat34 &mat, Matrix4Val src);
	extern void ConvertProjectionMatrix(D3DMATRIX &mat, Matrix4Val src, float zBias);
#endif

namespace enf
{
#define DEGENERATE_EPSILON	0.1f
#define BOGUS_RANGE 131072
#define ON_EPSILON 0.1f


ShapeCollection*	ShapeCollection::m_pCurrent = NULL;
#define LOCK(coll) 	ScopeLockSection lock(coll ? coll->m_CriticalSection : ShapeCollection::m_pCurrent->m_CriticalSection);


//#define LOG_SHAPES

//-----------------------------------------------------------------------
#ifndef ENF_SHAPES_DUMMYRENDER
	static bool RenderDisabled() 
	{
		//return false;
		return GEngineDD->IsDummy() || GEngineDD->GetDirect3DDevice().IsNull();
	}
#endif

//-----------------------------------------------------------------------
struct ShapeMesh
{
	ENF_DECLARE_SMALLOBJECT(ShapeMesh);
	uint		NumIndices;
	uint		NumVerts;

	ShapeMesh(): NumIndices(0), NumVerts(0) {}

	#ifdef ENF_SHAPES_DUMMYRENDER
		ShapeMesh(uint numIndices, uint numVerts, uint flags): NumIndices(numIndices), NumVerts(numVerts) 
		{}

		~ShapeMesh()
		{}

		bool LockIndexBuffer(uint Flags, void** ppData)
		{
			*ppData = NULL;
			return false;
		}

		void UnlockIndexBuffer()
		{}

		bool LockVertexBuffer(uint Flags, void** ppData)
		{
			*ppData = NULL;
			return false;
		}

		void UnlockVertexBuffer()
		{}
	#else
		ComRef<IDirect3DVertexBuffer9> buffVB;
		ComRef<IDirect3DIndexBuffer9> buffIB;

		VertexStaticData9::BufferFuture	VB;
		IndexStaticData9::BufferFuture	IB;
  
		ShapeMesh(uint numIndices, uint numVerts, uint flags): NumIndices(numIndices), NumVerts(numVerts) 
		{
			if (RenderDisabled())
				return;

			ComRef<IDirect3DVertexBuffer9> vb;
			GEngineDD->GetDirect3DDevice()->CreateVertexBuffer(sizeof(Vector3)*numVerts, flags, 0, D3DPOOL_DEFAULT, vb.Init(), NULL);

			ComRef<IDirect3DIndexBuffer9> ib;
			GEngineDD->GetDirect3DDevice()->CreateIndexBuffer(sizeof(short) * numIndices, flags, D3DFMT_INDEX16, D3DPOOL_DEFAULT, ib.Init(), NULL);
		
			VB = VertexStaticData9::BufferFuture(vb);
			IB = IndexStaticData9::BufferFuture(ib);

			buffVB = vb;
			buffIB = ib;
		}

		~ShapeMesh()
		{
			VB = VertexStaticData9::BufferFuture(VertexStaticData9::BufferFuture::Null);
			IB = IndexStaticData9::BufferFuture(IndexStaticData9::BufferFuture::Null);
		}

		bool LockIndexBuffer(uint Flags, void** ppData)
		{
			if (!buffIB)
			{
				*ppData = NULL;
				return false;
			}

			buffIB->Lock(0, NumIndices, ppData, Flags);
			if(*ppData != NULL)
				return true;
    
			return false;
		}

		void UnlockIndexBuffer()
		{
			if (!buffIB)
				return;

			buffIB->Unlock();
		}

		bool LockVertexBuffer(uint Flags, void** ppData)
		{
			if (!buffVB)
			{
				*ppData = NULL;
				return false;
			}

			buffVB->Lock(0, NumVerts, ppData, Flags);
			if(*ppData != NULL)
				return true;
    
			return false;
		}

		void UnlockVertexBuffer()
		{
			if (!buffVB)
				return;

			buffVB->Unlock();
		}
	#endif

  uint GetNumVertices() const
	{
		return NumVerts;
	}
	uint GetNumFaces() const
	{
		return NumIndices / 3;
	}
};

//-----------------------------------------------------------------------
ShapeMesh* CreateShapeMesh(bool dynamic, uint numFaces, uint numVertexes)
{
	uint numIndices = numFaces * 3;
	uint flags = 0;

	#ifndef ENF_SHAPES_DUMMYRENDER
		flags = dynamic ? D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC : D3DUSAGE_WRITEONLY;
	#endif

	ShapeMesh* mesh = ENF_NEW ShapeMesh(numIndices, numVertexes, flags);

	return mesh;
}

//-----------------------------------------------------------------------
void Shape::Insert(Winding* wnd)
{
	Winding** prev;
	for(prev = &m_pWindings; *prev; prev = &((*prev)->Next));
	*prev = wnd;
	wnd->Next = NULL;
}


//-----------------------------------------------------------------------
Shape* Shape::Create(uint flags, ShapeCollection* coll)
{
	Shape* sh;
	if(flags & SS_NOINSERT)
		sh = ENF_NEW Shape(NULL);
	else
	{
		if(coll == NULL)
			coll = ShapeCollection::m_pCurrent;

		//return dummy shape when we have no size for another shape
		if (coll->IsFull())
			return coll->m_pDummyShape;

		Assert(coll != NULL);
		sh = ENF_NEW Shape(coll);
	}
	sh->SetFlags(flags & ~(SS_NOINSERT));
  if(flags & SS_ONCE)
    sh->AddRef();
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::Create(Shape* clone, uint flags)
{
	LOCK(ShapeCollection::m_pCurrent)

	Assert(ShapeCollection::m_pCurrent != NULL);
	Shape* sh = ENF_NEW Shape(ShapeCollection::m_pCurrent);
	sh->m_Flags = clone->m_Flags | SS_INSTANCED | flags;
	sh->m_Type = clone->m_Type;
	sh->m_pMesh = clone->m_pMesh;
	sh->m_BBox = clone->m_BBox;
	sh->m_OutlineColor = clone->m_OutlineColor;
	sh->m_Color = clone->m_Color;
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateConvex(const Vector4* planes, uint stride, uint num, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)
	Ref<Shape> sh = Create(flags, coll);

	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_POLYGON;
	sh->m_BBox.Init();

	#define GET_PLANE(planes, index) *(Vector4*)(((ubyte*)planes) + index * stride)

	for (uint i = 0 ; i < num ; i++)
	{
		Winding* w = Winding::BaseWindingForPlane(GET_PLANE(planes, i));

		for (uint j = 0 ; j < num && w; j++)
		{
			if (i == j)
				continue;

			Vector4 p = GET_PLANE(planes, j) * -1.0f;
      Vector3 p3(p.x, p.y, p.z);
			w = Winding::ChopInPlace(w, p3, p.w, 0);
		}

		if(w == NULL)
			continue;

		sh->m_BBox.Add(w->Bounds());
		sh->Insert(w);
	}
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateBox(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)

	Ref<Shape> sh = Create(flags, coll);
	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_BBOX;

	Winding* wnd = Winding::Alloc(36);
	sh->Insert(wnd);

	Vector3* p = wnd->AppendPoints(36);

//Z axis
  p[0]._e[0] = mins._e[0]; p[0]._e[1] = maxs._e[1]; p[0]._e[2] = maxs._e[2];
	p[1]._e[0] = maxs._e[0]; p[1]._e[1] = maxs._e[1]; p[1]._e[2] = maxs._e[2];
	p[2]._e[0] = maxs._e[0]; p[2]._e[1] = mins._e[1]; p[2]._e[2] = maxs._e[2];
	p[3]._e[0] = mins._e[0]; p[3]._e[1] = maxs._e[1]; p[3]._e[2] = maxs._e[2];
	p[4]._e[0] = maxs._e[0]; p[4]._e[1] = mins._e[1]; p[4]._e[2] = maxs._e[2];
	p[5]._e[0] = mins._e[0]; p[5]._e[1] = mins._e[1]; p[5]._e[2] = maxs._e[2];
	p[6]._e[0] = maxs._e[0]; p[6]._e[1] = mins._e[1]; p[6]._e[2] = mins._e[2];
	p[7]._e[0] = maxs._e[0]; p[7]._e[1] = maxs._e[1]; p[7]._e[2] = mins._e[2];
	p[8]._e[0] = mins._e[0]; p[8]._e[1] = maxs._e[1]; p[8]._e[2] = mins._e[2];
	p[9]._e[0] = mins._e[0]; p[9]._e[1] = mins._e[1]; p[9]._e[2] = mins._e[2];
	p[10]._e[0] = maxs._e[0]; p[10]._e[1] = mins._e[1]; p[10]._e[2] = mins._e[2];
	p[11]._e[0] = mins._e[0]; p[11]._e[1] = maxs._e[1]; p[11]._e[2] = mins._e[2];
//Y axis
	p[12]._e[0] = mins._e[0]; p[12]._e[2] = maxs._e[2]; p[12]._e[1] = mins._e[1];
	p[13]._e[0] = maxs._e[0]; p[13]._e[2] = maxs._e[2]; p[13]._e[1] = mins._e[1];
	p[14]._e[0] = maxs._e[0]; p[14]._e[2] = mins._e[2]; p[14]._e[1] = mins._e[1];
	p[15]._e[0] = mins._e[0]; p[15]._e[2] = maxs._e[2]; p[15]._e[1] = mins._e[1];
	p[16]._e[0] = maxs._e[0]; p[16]._e[2] = mins._e[2]; p[16]._e[1] = mins._e[1];
	p[17]._e[0] = mins._e[0]; p[17]._e[2] = mins._e[2]; p[17]._e[1] = mins._e[1];
	p[18]._e[0] = maxs._e[0]; p[18]._e[2] = mins._e[2]; p[18]._e[1] = maxs._e[1];
	p[19]._e[0] = maxs._e[0]; p[19]._e[2] = maxs._e[2]; p[19]._e[1] = maxs._e[1];
	p[20]._e[0] = mins._e[0]; p[20]._e[2] = maxs._e[2]; p[20]._e[1] = maxs._e[1];
	p[21]._e[0] = mins._e[0]; p[21]._e[2] = mins._e[2]; p[21]._e[1] = maxs._e[1];
	p[22]._e[0] = maxs._e[0]; p[22]._e[2] = mins._e[2]; p[22]._e[1] = maxs._e[1];
	p[23]._e[0] = mins._e[0]; p[23]._e[2] = maxs._e[2]; p[23]._e[1] = maxs._e[1];
//X axis
	p[24]._e[2] = mins._e[2]; p[24]._e[1] = maxs._e[1]; p[24]._e[0] = mins._e[0];
	p[25]._e[2] = maxs._e[2]; p[25]._e[1] = maxs._e[1]; p[25]._e[0] = mins._e[0];
	p[26]._e[2] = maxs._e[2]; p[26]._e[1] = mins._e[1]; p[26]._e[0] = mins._e[0];
	p[27]._e[2] = mins._e[2]; p[27]._e[1] = maxs._e[1]; p[27]._e[0] = mins._e[0];
	p[28]._e[2] = maxs._e[2]; p[28]._e[1] = mins._e[1]; p[28]._e[0] = mins._e[0];
	p[29]._e[2] = mins._e[2]; p[29]._e[1] = mins._e[1]; p[29]._e[0] = mins._e[0];
	p[30]._e[2] = maxs._e[2]; p[30]._e[1] = mins._e[1]; p[30]._e[0] = maxs._e[0];
	p[31]._e[2] = maxs._e[2]; p[31]._e[1] = maxs._e[1]; p[31]._e[0] = maxs._e[0];
	p[32]._e[2] = mins._e[2]; p[32]._e[1] = maxs._e[1]; p[32]._e[0] = maxs._e[0];
	p[33]._e[2] = mins._e[2]; p[33]._e[1] = mins._e[1]; p[33]._e[0] = maxs._e[0];
	p[34]._e[2] = maxs._e[2]; p[34]._e[1] = mins._e[1]; p[34]._e[0] = maxs._e[0];
	p[35]._e[2] = mins._e[2]; p[35]._e[1] = maxs._e[1]; p[35]._e[0] = maxs._e[0];
	sh->m_BBox.Init(mins, maxs);
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreatePoint(const Vector3& p, float size, uint color, uint flags, ShapeCollection* coll)
{
	return CreateBox(p - Vector3(size), p + Vector3(size), color, flags, coll);
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateLines(const Vector3* points, uint num, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)

	Ref<Shape> sh = Create(flags, coll);
	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_LINE;

	Winding* w = Winding::Create(points, num);
	sh->Insert(w);
	sh->m_BBox = w->Bounds();
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//--------------------------------------------------------------------------------------
void GenerateSphereGeometry( uint numSlices, uint numStacks, float radius, Vector3* pData)
{
	for( uint i = 0; i < numSlices + 1; i++ )
	{
		for( uint j = 0; j < numStacks + 1; j++ )
		{
			float fTheta = float( i ) / numSlices * 2 * ENF_PI;
			float fPhi = ( float( j ) / numStacks * 2 - 1.0f ) * ENF_PI_HALF;
			pData->_e[0] = cosf( fTheta ) * cosf( fPhi ) * radius;
			pData->_e[2] = sinf( fTheta ) * cosf( fPhi ) * radius;
			pData->_e[1] = sinf( fPhi ) * radius;
			pData++;
		}
	}
}

//--------------------------------------------------------------------------------------
void GenerateSphereIndices( uint dwNumSlices, uint dwNumStacks, ushort* pIndices )
{
	short i0 = ( short )0;
	short i1 = ( short )1;
	short i2 = ( short )dwNumStacks + 2;
	short i3 = ( short )dwNumStacks + 1;

	for( uint i = 0; i < dwNumSlices; i++ )
	{
		for( uint j = 0; j < dwNumStacks; j++ )
		{
			pIndices[0] = i0;
			pIndices[1] = i3;
			pIndices[2] = i1;
			pIndices[3] = i1;
			pIndices[4] = i3;
			pIndices[5] = i2;
			pIndices += 6;
			i0++;
			i1++;
			i2++;
			i3++;
		}
		i0++;
		i1++;
		i2++;
		i3++;
	}
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateSphere(const Vector3& pos, float radius, uint color, uint flags, ShapeCollection* coll)
{
	//return immediately dummy shape
	if (((flags & SS_NOINSERT) == 0) && ((!coll && ShapeCollection::m_pCurrent->IsFull()) || (coll && coll->IsFull())))
		return Create(flags, coll);
		
	float rlen = ENF_PI2 * radius * 32.0f;
	uint numSlices = enf_limit((uint)(rlen / 8.0f), 8U, 24U);

	uint numVertexes = ( numSlices + 1 ) * ( numSlices + 1 );
	uint numFaces = 2 * numSlices * numSlices;

	ShapeMesh* pmesh = CreateShapeMesh((flags & SS_ONCE) != 0, numFaces, numVertexes);

	if(pmesh)
	{
		LOCK(coll)

		Ref<Shape> sh = Create(flags, coll);
		sh->m_Type = SHAPE_INDEXEDMESH;

		Vector3* pVertexData;
		if(pmesh->LockVertexBuffer(0, (void**)&pVertexData))
		{
			GenerateSphereGeometry( numSlices, numSlices, radius, pVertexData);
			pmesh->UnlockVertexBuffer();
		}

		ushort* pIndexData;
    if(pmesh->LockIndexBuffer(0, (void**)&pIndexData))
		{
			GenerateSphereIndices( numSlices, numSlices, pIndexData );
			pmesh->UnlockIndexBuffer();
		}
		sh->m_pMesh = pmesh;
		sh->SetMatrix(Matrix43(M3Identity, pos));
		sh->m_BBox.InitFromSphere(radius);
		if(flags & SS_WIREFRAME)
			sh->SetOutlineColor(color);
		else
			sh->SetColor(color);
		return sh;
	}

	return NULL;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateLine(const Vector3& p1, const Vector3& p2, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)

	Ref<Shape> sh = Create(flags, coll);
	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_LINE;

	sh->m_BBox.Init(p1);
	sh->m_BBox.Add(p2);

	Winding* w = Winding::Alloc(2);
	w->AddPoint(p1);
	w->AddPoint(p2);
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	sh->Insert(w);
	return sh;
}
//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateLine(const Vector2& p1, const Vector2& p2, uint color, uint flags, ShapeCollection* coll)
{
	return CreateLine(Vector3(p1.x, 0.0f, p1.y), Vector3(p2.x, 0.0f, p2.y), color, flags, coll);
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreatePyramid(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)
	Ref<Shape> sh = Create(flags, coll);

	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_MESH;

	Winding* wnd = Winding::Alloc(18);
	sh->Insert(wnd);
	
	Vector3* p = wnd->AppendPoints(18);

	Vector3 top;

	top._e[0] = (mins._e[0] + maxs._e[0]) * 0.5f;
	top._e[1] = maxs._e[1];
	top._e[2] = (mins._e[2] + maxs._e[2]) * 0.5f;
		
	float ypos =  mins._e[1];

  p[0] = top;
  p[1]._e[0] = maxs._e[0]; p[1]._e[1] = ypos; p[1]._e[2] = maxs._e[2];
	p[2]._e[0] = mins._e[0]; p[2]._e[1] = ypos; p[2]._e[2] = maxs._e[2];
  p[3] = top;
	p[4]._e[0] = mins._e[0]; p[4]._e[1] = ypos; p[4]._e[2] = mins._e[2];
  p[5]._e[0] = maxs._e[0]; p[5]._e[1] = ypos; p[5]._e[2] = mins._e[2];

  p[6] = top;
	p[7]._e[0] = maxs._e[0]; p[7]._e[1] = ypos; p[7]._e[2] = mins._e[2];
  p[8]._e[0] = maxs._e[0]; p[8]._e[1] = ypos; p[8]._e[2] = maxs._e[2];

  p[9] = top;
	p[10]._e[0] = mins._e[0]; p[10]._e[1] = ypos; p[10]._e[2] = maxs._e[2];
  p[11]._e[0] = mins._e[0]; p[11]._e[1] = ypos; p[11]._e[2] = mins._e[2];

  p[12]._e[0] = maxs._e[0]; p[12]._e[1] = ypos; p[12]._e[2] = maxs._e[2];
	p[13]._e[0] = maxs._e[0]; p[13]._e[1] = ypos; p[13]._e[2] = mins._e[2];
  p[14]._e[0] = mins._e[0]; p[14]._e[1] = ypos; p[14]._e[2] = maxs._e[2];

  p[15]._e[0] = maxs._e[0]; p[15]._e[1] = ypos; p[15]._e[2] = mins._e[2];
	p[16]._e[0] = mins._e[0]; p[16]._e[1] = ypos; p[16]._e[2] = mins._e[2];
  p[17]._e[0] = mins._e[0]; p[17]._e[1] = ypos; p[17]._e[2] = maxs._e[2];



	sh->m_BBox.Init(mins, maxs);
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateDiamond(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)
	Ref<Shape> sh = Create(flags, coll);

	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_Type = SHAPE_MESH;

	Winding* wnd = Winding::Alloc(24);
	sh->Insert(wnd);
	
	Vector3* p = wnd->AppendPoints(24);

	Vector3 top, down;
	float midz = (mins._e[1] + maxs._e[1]) * 0.5f;

	down._e[0] = top._e[0] = (mins._e[0] + maxs._e[0]) * 0.5f;
	down._e[2] = top._e[2] = (mins._e[2] + maxs._e[2]) * 0.5f;
	top._e[1] = maxs._e[1];
	down._e[1] = mins._e[1];

	p[0]._e[0] = mins._e[0]; p[0]._e[1] = midz; p[0]._e[2] = maxs._e[2];
	p[1]._e[0] = maxs._e[0]; p[1]._e[1] = midz; p[1]._e[2] = maxs._e[2];
	p[2] = top;
	p[3]._e[0] = maxs._e[0]; p[3]._e[1] = midz; p[3]._e[2] = maxs._e[2];
	p[4]._e[0] = mins._e[0]; p[4]._e[1] = midz; p[4]._e[2] = maxs._e[2];
	p[5] = down;
	p[6]._e[0] = maxs._e[0]; p[6]._e[1] = midz; p[6]._e[2] = mins._e[2];
	p[7]._e[0] = mins._e[0]; p[7]._e[1] = midz; p[7]._e[2] = mins._e[2];
	p[8] = top;
	p[9]._e[0] = mins._e[0]; p[9]._e[1] = midz; p[9]._e[2] = mins._e[2];
	p[10]._e[0] = maxs._e[0]; p[10]._e[1] = midz; p[10]._e[2] = mins._e[2];
	p[11] = down;


	p[12]._e[0] = maxs._e[0]; p[12]._e[1] = midz; p[12]._e[2] = maxs._e[2];
	p[13]._e[0] = maxs._e[0]; p[13]._e[1] = midz; p[13]._e[2] = mins._e[2];
	p[14] = top;
	p[15]._e[0] = maxs._e[0]; p[15]._e[1] = midz; p[15]._e[2] = mins._e[2];
	p[16]._e[0] = maxs._e[0]; p[16]._e[1] = midz; p[16]._e[2] = maxs._e[2];
	p[17] = down;

	p[18]._e[0] = mins._e[0]; p[18]._e[1] = midz; p[18]._e[2] = mins._e[2];
	p[19]._e[0] = mins._e[0]; p[19]._e[1] = midz; p[19]._e[2] = maxs._e[2];
	p[20] = top;
	p[21]._e[0] = mins._e[0]; p[21]._e[1] = midz; p[21]._e[2] = maxs._e[2];
	p[22]._e[0] = mins._e[0]; p[22]._e[1] = midz; p[22]._e[2] = mins._e[2];
	p[23] = down;

	sh->m_BBox.Init(mins, maxs);
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateMesh(const Vector3* vertexes, uint num, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)
	Ref<Shape> sh = Create(flags, coll);

	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->m_BBox.Init();
	sh->AddMesh(vertexes, num);
	if(flags & SS_WIREFRAME)
		sh->SetOutlineColor(color);
	else
		sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateIndexedMesh(const Vector3* vertexes, uint numVertexes, uint vertexStride, const ushort* indexes, uint numFaces, uint indexStride, float scale, uint color, uint flags, ShapeCollection* coll)
{
	//return immediately dummy shape
	if (((flags & SS_NOINSERT) == 0) && ((!coll && ShapeCollection::m_pCurrent->IsFull()) || (coll && coll->IsFull())))
		return Create(flags, coll);

	ShapeMesh* pMesh = CreateShapeMesh((flags & SS_ONCE) != 0, numFaces, numVertexes);
	if(pMesh)
	{
		LOCK(coll)

		Ref<Shape> sh = Create(flags, coll);

		sh->m_BBox.Init();
		Vector3* pVertexData;
		if(pMesh->LockVertexBuffer(0, (void**)&pVertexData))
		{
			for(uint v = 0; v < numVertexes; v++)
			{
				const Vector3 pos = *(const Vector3*)((ubyte*)vertexes + vertexStride * v) * scale;
				sh->m_BBox.Add(pos);
				pVertexData[v] = pos;
			}

			pMesh->UnlockVertexBuffer();
		}

		#ifndef ENF_SHAPES_DUMMYRENDER
			Assert(sh->m_BBox.IsValid());
			Assert(!BadVec(sh->m_BBox.GetMins()));
			Assert(!BadVec(sh->m_BBox.GetMaxs()));
		#endif

		ushort* pIndexData;
		if(pMesh->LockIndexBuffer(0, (void**)&pIndexData))
		{
			for(uint i = 0; i < numFaces * 3; i++)
			{
				ushort index = *(const ushort*)((ubyte*)indexes + indexStride * i);
				Assert(index < numVertexes);
				pIndexData[i] = index;
			}

			pMesh->UnlockIndexBuffer();
		}
		sh->m_Type = SHAPE_INDEXEDMESH;
		sh->m_pMesh = pMesh;
		if(flags & SS_WIREFRAME)
			sh->SetOutlineColor(color);
		else
			sh->SetColor(color);
		return sh;
	}

	return NULL;
}

//-----------------------------------------------------------------------
bool ShapeCollection::Grow()
{
	uint newSize = m_iSize << 1;
  Shape** nshapes = (Shape**)GMemFunctions->Realloc(m_pShapes, sizeof(Shape*) * newSize);

	uint n;
	for(n = m_iSize; (n < newSize - 1); n++)
	{
		nshapes[n] = (Shape*)(n + 1);
	}
	nshapes[n] = (Shape*)~0L;

	m_iFirstFree = m_iSize;
	m_iSize = newSize;
	m_pShapes = nshapes;
	return true;
}

//-----------------------------------------------------------------------
bool ShapeCollection::Remove(Shape* sh)
{
	ScopeLockSection lock (m_CriticalSection);

	#ifdef LOG_SHAPES
		LogF("Removing from collection 0x%X", sh);
	#endif

	Assert(m_pShapes[sh->m_iNum] == sh);

	if(m_pShapes[sh->m_iNum] != sh)
		return false;

	Assert(m_iCount);
	m_iCount--;
	m_pShapes[sh->m_iNum] = (Shape*)m_iFirstFree;
	m_iFirstFree = sh->m_iNum;
	
	sh->m_pCollection = NULL;
	sh->Release();

	return true;
}

//-----------------------------------------------------------------------
void ShapeCollection::Insert(Shape* sh)
{
	ScopeLockSection lock (m_CriticalSection);

	#ifdef LOG_SHAPES
		LogF("Inserting int collection 0x%X", sh);
	#endif

	Assert(m_iCount < MAX_SIZE);

	if(m_iFirstFree == ~0L)
	{
		if(!Grow())
			return;
	}

	Assert(m_pShapes[m_iFirstFree] == (const Shape*)~0L || m_pShapes[m_iFirstFree] < (const Shape*)MAX_SIZE);
	uint nextfree = (uint)m_pShapes[m_iFirstFree];

	m_iCount++;
	m_pShapes[m_iFirstFree] = sh;
	sh->m_iNum = m_iFirstFree;
	m_iFirstFree = nextfree;
}

//-----------------------------------------------------------------------
ShapeCollection::~ShapeCollection()
{
	m_pDummyShape->Release();

	for(uint n = 0; n < m_iSize; n++)
	{
		if(!IsValid(m_pShapes[n]))
			continue;

		delete m_pShapes[n];
	}
	if(m_pShapes)
	{
		GMemFunctions->Delete(m_pShapes);
		m_pShapes = NULL;
	}
	
	if(m_pCurrent == this)
		m_pCurrent = NULL;
}


//-----------------------------------------------------------------------
ShapeCollection::ShapeCollection():
	m_iSize(INITIAL_SIZE),
	m_iCount(0),
	m_iFirstFree(0),
	m_iLastFrame(0)
{
	m_pShapes = (Shape**)GMemFunctions->New(sizeof(Shape*) * INITIAL_SIZE, __FILE__, __LINE__);

	for(uint n = 0; n < (m_iSize - 1); n++)
		m_pShapes[n] = (Shape*)(n + 1);

	m_pShapes[m_iSize - 1] = (Shape*)~0L;

	m_pDummyShape = Shape::Create(SS_NOINSERT);
	m_pDummyShape->m_Flags |= SS_DUMMY;
}

//-----------------------------------------------------------------------
class CCompareShapes
{
public:
	inline int operator()(const Shape* e1, const Shape* e2) const
	{
		return e1->Compare(e2);
	}
};

//-----------------------------------------------------------------------
void ShapeCollection::FlushShapes(int flag)
{
	ScopeLockSection lock (m_CriticalSection);
	uint count = m_iCount;

	for(uint n = 0; n < m_iSize && count; n++)
	{
		Shape* sh = m_pShapes[n];
		if(!IsValid(sh))
			continue;

		count--;
		if(sh->m_Flags & flag)
			Remove(sh);
	}
}

//-----------------------------------------------------------------------
void ShapeCollection::SetHWDepthBias(float bias)
{
	#ifndef ENF_SHAPES_DUMMYRENDER
		static float depth = 0.000001f;

		float b = depth * bias;
		GEngineDD->D3DSetRenderState(-1, D3DRS_DEPTHBIAS, *((uint*)&b));
	
		b = bias > 0 ? 1.0f : -1.0f;
		GEngineDD->D3DSetRenderState(-1, D3DRS_SLOPESCALEDEPTHBIAS, *((uint*)&b));
	#endif
}

//-----------------------------------------------------------------------
void ShapeCollection::RenderFirstPass(Shape** firstpass, uint nfirstpass, float bias, int cb)
{
#ifndef ENF_SHAPES_DUMMYRENDER

  QuickSortCmp<Shape*, CCompareShapes>(nfirstpass, firstpass);
	ComRef<IDirect3DDevice9> &device = GEngineDD->GetDirect3DDevice()->GetRefForCB();

  device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

	float biasVec[4] = {0, 0, bias, 0};
  device->SetVertexShaderConstantF(FREESPACE_START_REGISTER0, biasVec, 1);

	uint lflags = 0xffffffff;
	uint lcolor = ~(firstpass[0]->m_Color);
	bool ident = false;

	for(uint n = 0; n < nfirstpass; n++)
	{
		Shape* sh = firstpass[n];

		if(sh->m_Color != lcolor)
		{
			lcolor = sh->m_Color;
      device->SetPixelShaderConstantF(0, (ColorP((long)lcolor)).GetArray(), 1);
		}

		if(sh->m_Flags & SS_IDENTMATRIX)
		{
			if(!ident)
			{
				ident = true;

				MatrixFloat34 zeroMatrix = {1,0,0,0,0,1,0,0,0,0,1,0};
        device->SetVertexShaderConstantF(VSC_EnfShapeWorldMatrix, (const float*) zeroMatrix, 3);
			}
		}
		else
		{
			ident = false;

			MatrixFloat34 worldMatrix;
			::ConvertMatrixTransposed3(worldMatrix, sh->m_Matrix);

			device->SetVertexShaderConstantF(VSC_EnfShapeWorldMatrix, (const float*) &worldMatrix, 3);
		}
		
		if(sh->m_Flags != lflags)
		{
			lflags = sh->m_Flags;

			if(lflags & SS_DOUBLESIDE)
			{
				device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			}
			else
			{
				device->SetRenderState(D3DRS_CULLMODE, (lflags & SS_BACKFACE) ? D3DCULL_CCW : D3DCULL_CW);
			}

			if(lflags & SS_TRANSP)
			{
				device->SetRenderState(D3DRS_ALPHABLENDENABLE, true);

				if (lflags & SS_ADDITIVE)
				{
					device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
					device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
				}
				else
				{
					device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
					device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
				}
			}
			else
			{
				device->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
			}

			device->SetRenderState(D3DRS_FILLMODE, (lflags & SS_WIREFRAME) ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

			switch(lflags & (SS_NOZWRITE|SS_NOZBUFFER))
			{
			case 0:
				device->SetRenderState(D3DRS_ZENABLE, true);
				device->SetRenderState(D3DRS_ZWRITEENABLE, true);
				device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				break;
			case SS_NOZWRITE:
				device->SetRenderState(D3DRS_ZENABLE, true);
				device->SetRenderState(D3DRS_ZWRITEENABLE, false);
				device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				break;
			case SS_NOZBUFFER:
				device->SetRenderState(D3DRS_ZENABLE, false);
				device->SetRenderState(D3DRS_ZWRITEENABLE, true);
				break;
			case (SS_NOZWRITE|SS_NOZBUFFER):
				device->SetRenderState(D3DRS_ZENABLE, false);
				device->SetRenderState(D3DRS_ZWRITEENABLE, false);
				break;
			}
		}
		
		sh->Render(0);
	}
#endif
}

//-----------------------------------------------------------------------
void ShapeCollection::RenderSecondPass(Shape** secondpass, uint nsecondpass, float bias)
{
#ifndef ENF_SHAPES_DUMMYRENDER
	QuickSortCmp<Shape*, CCompareShapes>(nsecondpass, secondpass);
	
	ComRef<IDirect3DDevice9> &device = GEngineDD->GetDirect3DDevice()->GetRefForCB();
	
	float zero[4] = {0,0,0,0};
	device->SetVertexShaderConstantF(VSC_EnfShapeBias, zero, 1);

	float color[4] = {0.7, 0.7, 0.7, 0.7};
	device->SetPixelShaderConstantF(0, color, 1);

	device->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	device->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	const ::Camera& camera = *::GScene->GetCamera();

	D3DXMATRIX projMatrix;
	ConvertProjectionMatrix(projMatrix, camera.ProjectionNormal(), bias);

	device->SetVertexShaderConstantF(VSC_EnfShapeProjMatrix, (const float*)&projMatrix, 4);

	uint lflags = 0xffffffff;
	uint lcolor = ~(secondpass[0]->m_OutlineColor);
	bool ident = false;

	for(uint n = 0; n < nsecondpass; n++)
	{
		Shape* sh = secondpass[n];

		if(sh->m_OutlineColor != lcolor)
		{
			lcolor = sh->m_OutlineColor;
			device->SetPixelShaderConstantF(0, (ColorP((long)lcolor)).GetArray(), 1);
		}

		if(sh->m_Flags & SS_IDENTMATRIX)
		{
			if(!ident)
			{
				ident = true;

				MatrixFloat34 zeroMatrix = {1,0,0,0,0,1,0,0,0,0,1,0};
				device->SetVertexShaderConstantF(VSC_EnfShapeWorldMatrix, (const float*) zeroMatrix, 3);
			}
		}
		else
		{
			ident = false;

			MatrixFloat34 worldMatrix;
			::ConvertMatrixTransposed3(worldMatrix, sh->m_Matrix);

			device->SetVertexShaderConstantF(VSC_EnfShapeWorldMatrix, (const float*) worldMatrix, 3);
		}

		if(sh->m_Flags != lflags)
		{
			lflags = sh->m_Flags;

			switch(lflags & (SS_NOZWRITE|SS_NOZBUFFER))
			{
			case 0:
				device->SetRenderState(D3DRS_ZENABLE, true);
				device->SetRenderState(D3DRS_ZWRITEENABLE, true);
				device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				break;
			case SS_NOZWRITE:
				device->SetRenderState(D3DRS_ZENABLE, true);
				device->SetRenderState(D3DRS_ZWRITEENABLE, false);
				device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				break;
			case SS_NOZBUFFER:
				device->SetRenderState(D3DRS_ZENABLE, false);
				device->SetRenderState(D3DRS_ZWRITEENABLE, true);
				break;
			case (SS_NOZWRITE|SS_NOZBUFFER):
				device->SetRenderState(D3DRS_ZENABLE, false);
				device->SetRenderState(D3DRS_ZWRITEENABLE, false);
				break;
			}
		}

		sh->Render(1);
	}
#endif
}

#ifndef ENF_SHAPES_DUMMYRENDER
//-----------------------------------------------------------------------
class D3DDeviceState
{
	ComRef<IDirect3DDevice9> device;

	ComRef<IDirect3DIndexBuffer9>				savedIB;
	ComRef<IDirect3DVertexDeclaration9> savedVD;
	ComRef<IDirect3DVertexBuffer9>			savedVB;
	ComRef<IDirect3DVertexShader9>			savedVS;
	ComRef<IDirect3DPixelShader9>				savedPS;
	ComRef<IDirect3DSurface9>						savedDepth;
	D3DVIEWPORT9												savedVP;

	//32 should be enough for each effect, we do not want to use dynamic memory
	static const int maxRegs = 32;

	struct GPURegs
	{
		typedef float GPUReg[4];
		int _start;
		int _num;
		GPUReg savedRegs[maxRegs];
	} savedPSRegs, savedVSRegs;

	uint savedVBBytes, savedVBStride;

	static const int maxRS = 14;
	static const D3DRENDERSTATETYPE SaveRS[maxRS];
	DWORD savedRS[maxRS];

public:
	D3DDeviceState(ComRef<IDirect3DDevice9>& devD9, int startVSReg, int numVSReg, int startPSReg, int numPSReg)
	{
		device = devD9;

		device->GetIndices(savedIB.Init());		
		device->GetVertexDeclaration(savedVD.Init());
		device->GetStreamSource(0, savedVB.Init(), &savedVBBytes, &savedVBStride);
		device->GetVertexShader(savedVS.Init());
		device->GetPixelShader(savedPS.Init());
		device->GetViewport(&savedVP);
		device->GetDepthStencilSurface(savedDepth.Init());

		int items = sizeof(SaveRS)/sizeof(int);
		for (uint i = 0; i < items; i++)
			device->GetRenderState(SaveRS[i], &savedRS[i]);

		savedVSRegs._start = startVSReg;
		savedVSRegs._num = numVSReg;
		device->GetVertexShaderConstantF(startVSReg, (float*) &savedVSRegs.savedRegs[0], numVSReg);

		savedPSRegs._start = startPSReg;
		savedPSRegs._num = numPSReg;
		device->GetVertexShaderConstantF(startPSReg, (float*) &savedPSRegs.savedRegs[0], numPSReg);
	}
	
	~D3DDeviceState()
	{
		device->SetIndices(savedIB);		
		device->SetVertexDeclaration(savedVD);
		device->SetStreamSource(0, savedVB, savedVBBytes, savedVBStride);		
		device->SetVertexShader(savedVS);
		device->SetPixelShader(savedPS);
		device->SetViewport(&savedVP);
		device->SetDepthStencilSurface(savedDepth);
		
		int items = sizeof(SaveRS)/sizeof(int);
		for (uint i = 0; i < items; i++)
			device->SetRenderState(SaveRS[i], savedRS[i]);

		device->SetVertexShaderConstantF(savedVSRegs._start, (const float*) &savedVSRegs.savedRegs[0], savedVSRegs._num);
		device->SetVertexShaderConstantF(savedPSRegs._start, (const float*) &savedPSRegs.savedRegs[0], savedPSRegs._num);
	}
};

const D3DRENDERSTATETYPE D3DDeviceState::SaveRS[D3DDeviceState::maxRS] = 
{
	D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, 
	D3DRS_ALPHABLENDENABLE, D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_ZFUNC, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
	D3DRS_CLIPPING, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS
};
#endif


//-----------------------------------------------------------------------
void ShapeCollection::RenderShapes()
{
	ScopeLockSection lock(m_CriticalSection);

#ifndef ENF_SHAPES_DUMMYRENDER
	
	#ifdef LOG_SHAPES
		LogF("Rendering shapes");
	#endif

	PROFILE_SCOPE_GRF_EX(RenderDebugShapes,pp,SCOPE_COLOR_YELLOW);

	//dummy render with no shaders 
	if (!GEngineDD->GetEnfShapeCollectionResources() || RenderDisabled())
	{
		FlushShapes();
		return;
	}

	//new simulation frame begin? remove all processed shapes
	if (m_iLastFrame < Glob.frameID)
	{
		m_iLastFrame = Glob.frameID;
		FlushShapes(SS_PROCESSED);
	}

	if(m_iCount == 0)
		return;

  int cb = -1;

	//some preparations
	bool inScene = GEngineDD->_d3dFrameOpen;
	if (!inScene) 
		GEngineDD->D3DBeginScene();

	/*
	GEngineDD->FlushAndFreeAllQueues();
	GEngineDD->SwitchTL(cb, EngineDD9::TLViewspace);

	EngineShapeProperties prop;
	GEngineDD->DoPrepareTriangle(cb,NULL,TexMaterialLODInfo(NULL),0,NoZBuf|NoZWrite|ClampU|ClampV|FilterPoint,prop);
	GEngineDD->DoShadowsStatesSettings(cb,SS_Transparent,false);
	*/

	ComRef<IDirect3DDevice9> &device = GEngineDD->GetDirect3DDevice()->GetRefForCB();

	D3DDeviceState state(device, FREESPACE_START_REGISTER0, 12, 0, 2);

	static float _bias1 = 40.0f;
	static float _bias2 = 80.0f;
	float bias1, bias2;

  Assert(!GEngine->IsCBRecording());

  device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_ALPHA);
  device->SetRenderState(D3DRS_ALPHATESTENABLE, false);
	device->SetRenderState(D3DRS_STENCILENABLE, false);
	device->SetRenderState(D3DRS_CLIPPING, false);
	device->SetRenderState(D3DRS_DEPTHBIAS, 0);
	device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);

	bool msaa = GEngineDD->_rtMultiSampleType != D3DMULTISAMPLE_NONE;
	ComRefFake<IDirect3DSurface9> origDepth = msaa ? GEngineDD->_depthBufferRT : GEngineDD->_depthBuffer;	
	device->SetDepthStencilSurface(origDepth);

	D3DVIEWPORT9 vp;
	vp.X = 0;
	vp.Y = 0;
	vp.Width  = GEngineDD->Width();
	vp.Height = GEngineDD->Height();
	vp.MinZ = GEngineDD->_minZRange;
	vp.MaxZ = GEngineDD->_maxZRange;
	device->SetViewport(&vp);

	const ::Camera& camera = *::GScene->GetCamera();

	//matrices
	Matrix4 modelToWorldOffset(true);
	Vector3 camPos = -camera.Position() ;//-GEngineDD->CBState(-1)._invView.Position();
	modelToWorldOffset.SetPosition(camPos);
	
	Matrix4 viewZero;
	Matrix3 orientMat = camera.Orientation().InverseRotation();//GEngineDD->CBState(-1)._view.Orientation();
	viewZero.SetOrientation(orientMat);
	viewZero.SetPosition(VZero);
	
	Matrix4 viewWorld;
	viewWorld = viewZero * modelToWorldOffset;

	MatrixFloat34 viewWorldMatrix;
	ConvertMatrixTransposed3(viewWorldMatrix, viewWorld);
	device->SetVertexShaderConstantF(VSC_EnfShapeViewMatrix, (const float*) &viewWorldMatrix, 3);

	bias1 = _bias1;
	bias2 = _bias2;

	D3DXMATRIX projMatrix;
	ConvertProjectionMatrix(projMatrix, camera.ProjectionNormal(), bias1);

	device->SetVertexShaderConstantF(VSC_EnfShapeProjMatrix, (const float*)&projMatrix, 4);

	device->SetVertexDeclaration(GEngineDD->GetEnfShapeCollectionResources()->_vd);
	device->SetVertexShader(GEngineDD->GetEnfShapeCollectionResources()->_vs);
	device->SetPixelShader(GEngineDD->GetEnfShapeCollectionResources()->_ps);

	uint count = m_iCount;

	const int MAX_STACK_SHAPE_COUNT = 50000;
	int stackCount = intMin(MAX_STACK_SHAPE_COUNT, count);

	Shape** firstpass = ALLOCA(Shape*, stackCount);
	Shape** secondpass = ALLOCA(Shape*, stackCount);

	uint nfirstpass = 0;
	uint nsecondpass = 0;

	if (stackCount >= MAX_STACK_SHAPE_COUNT)
	{
		typedef Shape* pShape;
		firstpass  = new pShape[count];
		secondpass = new pShape[count];
	}


	for(uint n = 0; n < m_iSize && count; n++)
	{
		Shape* sh = m_pShapes[n];
		if(!IsValid(sh))
			continue;

		count--;
		
		if (sh->m_Flags & SS_ONCE)
			sh->m_Flags |= SS_PROCESSED;

		{
			if(!(sh->m_Flags & SS_VISIBLE))
				continue;

			if(!(m_pShapes[n]->m_Flags & SS_NOCULL))
			{
				Vector3 center = sh->m_BBox.GetCenter();
				float radius = sh->m_BBox.GetCorner().Distance(Vector3(0.0f));

				if(sh->m_Flags & SS_IDENTMATRIX)
				{
					if (camera.IsClippedByAnyPlane(center, radius, sh->m_BBox.GetMinMax()))
						continue;
				}
				else
				{
					ClipFlags f;
					Vector3 c (VMultiply, sh->m_Matrix, center);
					if (camera.IsClippedByAnyPlane(c, radius, sh->m_Matrix, sh->m_BBox.GetMinMax(), f))
						continue;
				}
			}

			//don't add to solid pass
			if(!(sh->m_Flags & SS_WIREFRAME))
				firstpass[nfirstpass++] = sh;

			if(!(sh->m_Flags & SS_NOOUTLINE) && sh->m_Type != SHAPE_LINE)
				secondpass[nsecondpass++] = sh;
		}
	}

	if(nfirstpass)
	{
		RenderFirstPass(firstpass, nfirstpass, bias1, cb);
	}

	if(nsecondpass)
	{
		RenderSecondPass(secondpass, nsecondpass, bias2);
	}

	if (stackCount >= MAX_STACK_SHAPE_COUNT)
	{
		delete [] firstpass;
		delete [] secondpass;
	}

	if (!inScene) 
		GEngineDD->D3DEndScene();

	//this must be when rendering as postprocess step
	//GEngineDD->SwitchTL(cb, EngineDD9::TLDisabled);
#endif

	//FlushShapes();

	#ifdef LOG_SHAPES
		LogF("End Rendering");
	#endif

}


//-----------------------------------------------------------------------
void Shape::Render(uint c) const
{
#ifndef ENF_SHAPES_DUMMYRENDER
	ShapeType type = m_Type;
	uint stride = sizeof(float) * 3;

	ComRef<IDirect3DDevice9> &device = GEngineDD->GetDirect3DDevice()->GetRefForCB();

	if(type == SHAPE_INDEXEDMESH)
	{
		const ShapeMesh* pmesh = m_pMesh;
		if(pmesh)
		{
			device->SetStreamSource(0, pmesh->buffVB, 0, stride);
			device->SetIndices(pmesh->buffIB);
			device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, pmesh->GetNumFaces()*3, 0, pmesh->GetNumFaces());
		}
	}
	else
	{
		if(type == SHAPE_BBOX)
		{
			const ::Vector3& mins = m_BBox.GetMins();
			const ::Vector3& maxs = m_BBox.GetMaxs();

			if(c == 0)
			{
				Vector3* p = ALLOCA(Vector3, 36);

				p[0]._e[0] = mins._e[0]; p[0]._e[1] = maxs._e[1]; p[0]._e[2] = maxs._e[2];
				p[1]._e[0] = maxs._e[0]; p[1]._e[1] = maxs._e[1]; p[1]._e[2] = maxs._e[2];
				p[2]._e[0] = maxs._e[0]; p[2]._e[1] = mins._e[1]; p[2]._e[2] = maxs._e[2];
				p[3]._e[0] = mins._e[0]; p[3]._e[1] = maxs._e[1]; p[3]._e[2] = maxs._e[2];
				p[4]._e[0] = maxs._e[0]; p[4]._e[1] = mins._e[1]; p[4]._e[2] = maxs._e[2];
				p[5]._e[0] = mins._e[0]; p[5]._e[1] = mins._e[1]; p[5]._e[2] = maxs._e[2];
				p[6]._e[0] = maxs._e[0]; p[6]._e[1] = mins._e[1]; p[6]._e[2] = mins._e[2];
				p[7]._e[0] = maxs._e[0]; p[7]._e[1] = maxs._e[1]; p[7]._e[2] = mins._e[2];
				p[8]._e[0] = mins._e[0]; p[8]._e[1] = maxs._e[1]; p[8]._e[2] = mins._e[2];
				p[9]._e[0] = mins._e[0]; p[9]._e[1] = mins._e[1]; p[9]._e[2] = mins._e[2];
				p[10]._e[0] = maxs._e[0]; p[10]._e[1] = mins._e[1]; p[10]._e[2] = mins._e[2];
				p[11]._e[0] = mins._e[0]; p[11]._e[1] = maxs._e[1]; p[11]._e[2] = mins._e[2];
			//Y axis
				p[12]._e[0] = mins._e[0]; p[12]._e[2] = maxs._e[2]; p[12]._e[1] = mins._e[1];
				p[13]._e[0] = maxs._e[0]; p[13]._e[2] = maxs._e[2]; p[13]._e[1] = mins._e[1];
				p[14]._e[0] = maxs._e[0]; p[14]._e[2] = mins._e[2]; p[14]._e[1] = mins._e[1];
				p[15]._e[0] = mins._e[0]; p[15]._e[2] = maxs._e[2]; p[15]._e[1] = mins._e[1];
				p[16]._e[0] = maxs._e[0]; p[16]._e[2] = mins._e[2]; p[16]._e[1] = mins._e[1];
				p[17]._e[0] = mins._e[0]; p[17]._e[2] = mins._e[2]; p[17]._e[1] = mins._e[1];
				p[18]._e[0] = maxs._e[0]; p[18]._e[2] = mins._e[2]; p[18]._e[1] = maxs._e[1];
				p[19]._e[0] = maxs._e[0]; p[19]._e[2] = maxs._e[2]; p[19]._e[1] = maxs._e[1];
				p[20]._e[0] = mins._e[0]; p[20]._e[2] = maxs._e[2]; p[20]._e[1] = maxs._e[1];
				p[21]._e[0] = mins._e[0]; p[21]._e[2] = mins._e[2]; p[21]._e[1] = maxs._e[1];
				p[22]._e[0] = maxs._e[0]; p[22]._e[2] = mins._e[2]; p[22]._e[1] = maxs._e[1];
				p[23]._e[0] = mins._e[0]; p[23]._e[2] = maxs._e[2]; p[23]._e[1] = maxs._e[1];
			//X axis
				p[24]._e[2] = mins._e[2]; p[24]._e[1] = maxs._e[1]; p[24]._e[0] = mins._e[0];
				p[25]._e[2] = maxs._e[2]; p[25]._e[1] = maxs._e[1]; p[25]._e[0] = mins._e[0];
				p[26]._e[2] = maxs._e[2]; p[26]._e[1] = mins._e[1]; p[26]._e[0] = mins._e[0];
				p[27]._e[2] = mins._e[2]; p[27]._e[1] = maxs._e[1]; p[27]._e[0] = mins._e[0];
				p[28]._e[2] = maxs._e[2]; p[28]._e[1] = mins._e[1]; p[28]._e[0] = mins._e[0];
				p[29]._e[2] = mins._e[2]; p[29]._e[1] = mins._e[1]; p[29]._e[0] = mins._e[0];
				p[30]._e[2] = maxs._e[2]; p[30]._e[1] = mins._e[1]; p[30]._e[0] = maxs._e[0];
				p[31]._e[2] = maxs._e[2]; p[31]._e[1] = maxs._e[1]; p[31]._e[0] = maxs._e[0];
				p[32]._e[2] = mins._e[2]; p[32]._e[1] = maxs._e[1]; p[32]._e[0] = maxs._e[0];
				p[33]._e[2] = mins._e[2]; p[33]._e[1] = mins._e[1]; p[33]._e[0] = maxs._e[0];
				p[34]._e[2] = maxs._e[2]; p[34]._e[1] = mins._e[1]; p[34]._e[0] = maxs._e[0];
				p[35]._e[2] = mins._e[2]; p[35]._e[1] = maxs._e[1]; p[35]._e[0] = maxs._e[0];
				device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 12, p, sizeof(::Vector3));
			}
			else 
			{
				Vector3 lines[12][2];

				lines[0][0] = mins;
				lines[0][1]._e[0] = mins._e[0];
				lines[0][1]._e[1] = mins._e[1];
				lines[0][1]._e[2] = maxs._e[2];

				lines[1][0] = lines[0][1];
				lines[1][1]._e[0] = maxs._e[0];
				lines[1][1]._e[1] = mins._e[1];
				lines[1][1]._e[2] = maxs._e[2];

				lines[2][0] = lines[1][1];
				lines[2][1]._e[0] = maxs._e[0];
				lines[2][1]._e[1] = mins._e[1];
				lines[2][1]._e[2] = mins._e[2];

				lines[3][0] = lines[2][1];
				lines[3][1] = mins;
	///
				lines[4][0]._e[0] = mins._e[0];
				lines[4][0]._e[1] = maxs._e[1];
				lines[4][0]._e[2] = mins._e[2];
				lines[4][1]._e[0] = mins._e[0];
				lines[4][1]._e[1] = maxs._e[1];
				lines[4][1]._e[2] = maxs._e[2];

				lines[5][0] = lines[4][1];
				lines[5][1] = maxs;

				lines[6][0] = lines[5][1];
				lines[6][1]._e[0] = maxs._e[0];
				lines[6][1]._e[1] = maxs._e[1];
				lines[6][1]._e[2] = mins._e[2];

				lines[7][0] = lines[6][1];
				lines[7][1] = lines[4][0];
	///
				lines[8][0] = lines[3][1];
				lines[8][1] = lines[4][0];

				lines[9][0] = lines[0][1];
				lines[9][1] = lines[4][1];

				lines[10][0] = lines[1][1];
				lines[10][1] = lines[5][1];

				lines[11][0] = lines[2][1];
				lines[11][1] = lines[6][1];

				device->DrawPrimitiveUP(D3DPT_LINELIST, 12, lines, sizeof(Vector3));
			}
		}
		else
		{
			for(const Winding* wnd = m_pWindings; wnd; wnd = wnd->Next)
			{
				if(type == SHAPE_MESH)
				{
					device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, wnd->GetNumPoints() / 3, wnd->p, sizeof(Vector3));
				}
				else if(type == SHAPE_LINE)
				{
					device->DrawPrimitiveUP(D3DPT_LINESTRIP, wnd->GetNumPoints() - 1, wnd->p, sizeof(Vector3));
				}
				else if(type == SHAPE_POLYGON)
				{
					if(c == 1)
					{
						device->DrawPrimitiveUP(D3DPT_LINESTRIP, wnd->GetNumPoints(), wnd->p, sizeof(Vector3));
					}
					else
					{
						device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, wnd->GetNumPoints() - 2 , wnd->p, sizeof(Vector3));
					}
				}
			}
		}
	}
#endif
}

//-----------------------------------------------------------------------
void Shape::AddMesh(const Vector3* vec, uint num)
{
	if (IsDummy())
		return;

	LOCK(m_pCollection)

	Assert(m_Type == SHAPE_NONE || m_Type == SHAPE_MESH);
	m_Type = SHAPE_MESH;

	Winding* w = Winding::Create(vec, num * 3);
	m_BBox.Add(w->Bounds());

	Insert(w);
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreatePolygon(const Vector3* vertexes, uint num, uint color, uint flags, ShapeCollection* coll)
{
	LOCK(coll)
	Ref<Shape> sh = Create(flags, coll);

	if (!sh)
		return NULL;

	if (sh->IsDummy())
		return sh;

	sh->AddPolygon(vertexes, num);
	sh->SetColor(color);
	return sh;
}

//-----------------------------------------------------------------------
Ref<Shape> Shape::CreatePlane(const Vector3& point, const Vector3& normal, float size, uint color, uint flags, ShapeCollection* coll)
{
	Vector3 tempdir = fabs(normal._e[2]) > 0.8 ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 0.0f, 1.0f);
  Vector3 dir1 = (normal.CrossProduct(tempdir)).Normalized();
  Vector3 dir2 = normal.CrossProduct(dir1);

	dir1 *= size;
	dir2 *= size;

	Vector3 poly[4];
	poly[0] = point - dir1 - dir2;
	poly[1] = point + dir1 - dir2;
	poly[2] = point + dir1 + dir2;
	poly[3] = point - dir1 + dir2;

	return CreatePolygon(poly, 4, color, flags, coll);
}

//-----------------------------------------------------------------------
void Shape::AddPolygon(const Vector3* vec, uint num)
{
	if (IsDummy())
		return;

	LOCK(m_pCollection)

	Assert(m_Type == SHAPE_NONE || m_Type == SHAPE_POLYGON);
	m_Type = SHAPE_POLYGON;

	Winding* w = Winding::Alloc(num + 1);
  w->SetPoints(vec, num);
  w->p[num] = w->p[0];

	m_BBox.Add(w->Bounds());

	Insert(w);
}

//--------------------------------------------------------------------------------------
void GenerateCylinderGeometry(const Vector3& pos, uint numSlices, float radius, float length, Vector3* pData, bool cylinder)
{
	float add = cylinder ? 0.0f : radius;

	for( uint i = 0; i < numSlices; i++ )
	{
		float fTheta = float( i ) / numSlices * ENF_PI2;
		pData->_e[0] = pos._e[0] + cosf( fTheta ) * radius;
		pData->_e[1] = pos._e[1] - length * 0.5f + add;
		pData->_e[2] = pos._e[2] + sinf( fTheta ) * radius;
		pData++;
	}
	pData->_e[0] = pos._e[0];
	pData->_e[1] = pos._e[1] - length * 0.5f;
	pData->_e[2] = pos._e[2];
	pData++;

	for( uint i = 0; i < numSlices; i++ )
	{
		float fTheta = float( i ) / numSlices * ENF_PI2;
		pData->_e[0] = pos._e[0] + cosf( fTheta ) * radius;
		pData->_e[1] = pos._e[1] + length * 0.5f - add;
		pData->_e[2] = pos._e[2] + sinf( fTheta ) * radius;
		pData++;
	}

	pData->_e[0] = pos._e[0];
	pData->_e[1] = pos._e[1] + length * 0.5f;
	pData->_e[2] = pos._e[2];
	pData++;
}

//--------------------------------------------------------------------------------------
void GenerateCylinderIndices( uint dwNumSlices, ushort* pIndices )
{
	//generate top trifan
	for(uint n = 0; n < dwNumSlices; n++)
	{
		pIndices[0] = dwNumSlices;
		pIndices[1] = n;
		pIndices[2] = (n + 1) % dwNumSlices;
		pIndices += 3;
	}

	uint offset = dwNumSlices + 1;
	//generate bottom trifan
	for(uint n = 0; n < dwNumSlices; n++)
	{
		pIndices[0] = offset + ((n + 1) % dwNumSlices);
		pIndices[1] = offset + n;
		pIndices[2] = offset + dwNumSlices;
		pIndices += 3;
	}

	//generate rest
	for(uint n = 0; n < dwNumSlices; n++)
	{
		pIndices[0] = n + offset;
		pIndices[1] = (n + 1) % dwNumSlices;
		pIndices[2] = n;
		pIndices[3] = offset + ((n + 1) % dwNumSlices);
		pIndices[4] = (n + 1) % dwNumSlices;
		pIndices[5] = n + offset;
		pIndices += 6;
	}
}


//-----------------------------------------------------------------------
Ref<Shape> Shape::CreateCylinderCapsule(const Vector3& position, float radius, float length, uint color, uint flags, ShapeCollection* coll, bool cylinder)
{
	float rlen = ENF_PI2 * radius * 32;
	uint numSlices = enf_limit((uint)(rlen / 8.0f), 8U, 24U);

	uint numVertexes = numSlices * 2 + 2;
	uint numFaces = (numSlices * 2) + (numSlices * 2);

	ShapeMesh* pmesh = CreateShapeMesh((flags & SS_ONCE) != 0, numFaces, numVertexes);
	if(pmesh)
	{
		LOCK(coll)

		Ref<Shape> sh = Create(flags, coll);
		sh->m_Type = SHAPE_INDEXEDMESH;

		Vector3* pVertexData;
		if(pmesh->LockVertexBuffer(0, (void**)&pVertexData))
		{
			GenerateCylinderGeometry(position, numSlices, radius, length, pVertexData, cylinder);
			pmesh->UnlockVertexBuffer();
		}

		ushort* pIndexData;
		if(pmesh->LockIndexBuffer(0, (void**)&pIndexData))
		{
			GenerateCylinderIndices( numSlices, pIndexData );
			pmesh->UnlockIndexBuffer();
		}
		sh->m_pMesh = pmesh;
		sh->m_BBox.Init(position - Vector3(radius, radius, length), position + Vector3(radius, radius, length));
		if(flags & SS_WIREFRAME)
			sh->SetOutlineColor(color);
		else
			sh->SetColor(color);
		return sh;
	}

	return NULL;
}

//--------------------------------------------------------------------------------------
void GenerateCapsuleGeometry( uint numSlices, uint numStacks, float radius, float length, Vector3* pData)
{
	for( uint j = 0; j < numStacks + 1; j++ )
	{
		float extZ = j < (numStacks / 2) ? length * 0.5f : length * -0.5f;

		for( uint i = 0; i < numSlices + 1; i++ )
		{
			float fTheta = float( i ) / numSlices * 2 * ENF_PI;
			float fPhi = ( float( j ) / numStacks * 2 - 1.0f ) * ENF_PI_HALF;
			pData->_e[0] = cosf( fTheta ) * cosf( fPhi ) * radius;
			pData->_e[2] = sinf( fTheta ) * cosf( fPhi ) * radius + extZ;
			pData->_e[1] = sinf( fPhi ) * radius;
			pData++;
		}
	}

}

//--------------------------------------------------------------------------------------
void GenerateCapsuleIndices( uint dwNumSlices, uint dwNumStacks, ushort* pIndices )
{
	short i0 = ( short )0;
	short i1 = ( short )1;
	short i2 = ( short )dwNumStacks + 2;
	short i3 = ( short )dwNumStacks + 1;

	for( uint i = 0; i < dwNumSlices; i++ )
	{
		for( uint j = 0; j < dwNumStacks; j++ )
		{
			pIndices[0] = i0;
			pIndices[1] = i3;
			pIndices[2] = i1;
			pIndices[3] = i1;
			pIndices[4] = i3;
			pIndices[5] = i2;
			pIndices += 6;
			i0++;
			i1++;
			i2++;
			i3++;
		}
		i0++;
		i1++;
		i2++;
		i3++;
	}
}
/*
//-----------------------------------------------------------------------
Shape* Shape::CreateCapsule(const Vector3& position, float radius, float length, uint color, uint flags)
{
	float rlen = ENF_PI2 * radius;
	uint numSlices = enf_limit((uint)(rlen / 8.0f), 8U, 24U);

	uint cylinderNumVertexes = numSlices * 2 + 2;
	uint cylinderNumFaces = (numSlices * 2) + (numSlices * 2);


	uint hemisphereNumVertexes = ( numSlices/2 + 1 ) * ( numSlices/2 + 1 ) * 2;
	uint hemisphereNumFaces = numSlices * numSlices;

	ShapeMesh* pmesh = CreateShapeMesh((flags & SS_ONCE) != 0, cylinderNumFaces + hemisphereNumFaces * 2, cylinderNumVertexes + hemisphereNumVertexes * 2);
	if(pmesh)
	{
		Shape* sh = Create(flags);
		sh->m_Type = SHAPE_INDEXEDMESH;

		Vector3* pVertexData;
		if(D3D_OK == pmesh->LockVertexBuffer(0, (void**)&pVertexData))
		{
			GenerateCapsuleGeometry( numSlices, numSlices, radius, length, pVertexData);
			pmesh->UnlockVertexBuffer();
		}

		ushort* pIndexData;
		if(D3D_OK == pmesh->LockIndexBuffer(0, (void**)&pIndexData))
		{
			GenerateCapsuleIndices( numSlices, numSlices, pIndexData );
			pmesh->UnlockIndexBuffer();
		}
		sh->m_pMesh = pmesh;
		sh->SetMatrix(Matrix43(M3Identity, position));
		float size = (radius > length) ? radius : length;
		sh->m_BBox.InitFromSphere(size);
		if(flags & SS_WIREFRAME)
			sh->SetOutlineColor(color);
		else
			sh->SetColor(color);
		return sh;
	}

	return NULL;
}
*/

//-----------------------------------------------------------------------
void Shape::Cleanup()
{
	if(m_Flags & SS_INSTANCED)
		return;
	
	#ifdef LOG_SHAPES
		LogF("Cleaning shape 0x%X", this);
	#endif
	
	if(m_Type == SHAPE_INDEXEDMESH)
	{
		SAFE_DELETE(m_pMesh)
	}
	else
	{
		Winding* next = NULL;
		for(Winding* wnd = m_pWindings; wnd; wnd = next)
		{
			next = wnd->Next;
			delete wnd;
		}
	}
}

//-----------------------------------------------------------------------
Shape::Shape(ShapeCollection* sc):
	m_Flags(SS_IDENTMATRIX|SS_VISIBLE),
	m_Type(SHAPE_NONE),
	m_pWindings(NULL),
	m_Color(0),
	m_OutlineColor(0x7f7f7f7f),
	m_iNum(-1),
	m_pCollection(sc),
	m_Matrix(false)
{
	if(sc)
		sc->Insert(this);
}

//-----------------------------------------------------------------------
Shape::~Shape ()
{
	LOCK(m_pCollection);

	if(m_pCollection)
	{
		if(!m_pCollection->Remove(this))
		{
			Assert(!"WTF??");
			return;
		}
	}

	Cleanup();
}

//-----------------------------------------------------------------------
void* Winding::operator new(NEW_SIZE_T size, char const * file, int line) THROW_NEW { Assert2(0, "unsupported!"); return NULL;}
void* Winding::operator new[](NEW_SIZE_T size, char const * file, int line ) THROW_NEW { Assert2(0, "unsupported!"); return NULL;}
void* Winding::operator new(NEW_SIZE_T size) THROW_NEW  { Assert2(0, "unsupported!"); return NULL;}
void* Winding::operator new[](NEW_SIZE_T size) THROW_NEW  { Assert2(0, "unsupported!"); return NULL;}

//-----------------------------------------------------------------------
void Winding::RemoveColinearPoints ()
{
	Vector3* tp = ALLOCA(Vector3, GetNumPoints());
	int numtp = 0;
	for (int i = 0; i < GetNumPoints(); i++)
	{
		int j = (i + 1) % GetNumPoints();
		int k = (i + GetNumPoints() - 1) % GetNumPoints();
		Vector3 v1 = (p[j] - p[i]).Normalized();
		Vector3 v2 = (p[i] - p[k]).Normalized();
		if (v1.DotProduct(v2) < 0.9999f)
		{
			tp[numtp++] = p[i];
		}
	}

	if (numtp == GetNumPoints())
		return;

	SetPoints(tp, numtp);
}

//-----------------------------------------------------------------------
Winding* Winding::BaseWindingForPlane(Vector4 plane)
{
// find the major axis
	float max = -BOGUS_RANGE;
	int x = -1;
	for (int i = 0 ;i < 3; i++)
	{
		float v = (float)fabs(plane[i]);
		if (v > max)
		{
			x = i;
			max = v;
		}
	}
	if (x==-1)
		return NULL;
		
	Vector3 vup;
	float v =plane.z;

	switch (x)
	{
	case 0:
	case 1:
		vup._e[0] = 0;
		vup._e[1] = 0;
		vup._e[2] = 1;
		break;		
	case 2:
		vup._e[0] = 1;
		vup._e[1] = 0;
		vup._e[2] = 0;
		v = plane.x;
		break;		
	}

	vup += Vector3(plane.v) * -v;
	vup.Normalize();

	Vector3 normal = Vector3(plane.v);
	Vector3 org = normal * plane.w;
  Vector3 vright = normal.CrossProduct(vup);
	
	vup *= BOGUS_RANGE;
	vright *= BOGUS_RANGE;

// project a really big	axis aligned box onto the plane
	Winding* w = Winding::Alloc(4);
	
	w->AddPoint(org - vright + vup);
	w->AddPoint(org + vright + vup);
	w->AddPoint(org + vright - vup);
	w->AddPoint(org - vright - vup);
	return w;	
}

//-----------------------------------------------------------------------
Winding* Winding::CopyReversed() const
{
	Winding* c = Alloc(GetNumPoints());

	for (int i = 0; i < GetNumPoints(); i++)
		c->AddPoint(p[GetNumPoints() - 1 - i]);

	return c;
}

//-----------------------------------------------------------------------
void Winding::ClipEpsilon(const Vector3& normal, float dist, float epsilon, Winding** front, Winding** back) const
{
	float* dists = ALLOCA(float, GetNumPoints() + 1);
	ClipSide* sides = ALLOCA(ClipSide, GetNumPoints() + 1);
	int counts[3];
	
	counts[0] = counts[1] = counts[2] = 0;

// determine sides for each point
	int i;
	for (i = 0; i < GetNumPoints(); i++)
	{
		float dot = normal.DotProduct(p[i]) - dist;
		dists[i] = dot;
		ClipSide side;
		if (dot > epsilon)
			side = SIDE_FRONT;
		else if (dot < -epsilon)
			side = SIDE_BACK;
		else
		{
			side = SIDE_ON;
		}
		sides[i] = side;
		counts[side]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];
	
	*front = *back = NULL;

	if (!counts[SIDE_FRONT])
	{
		*back = Copy();
		return;
	}
	if (!counts[SIDE_BACK])
	{
		*front = Copy();
		return;
	}

	int maxpts = GetNumPoints() + 4;	// cant use counts[0]+2 because
								// of fp grouping errors

	Winding* f = Alloc(maxpts);
	*front = f;
	Winding* b = Alloc(maxpts);
	*back = b;

	for (int i = 0; i < GetNumPoints(); i++)
	{
		const Vector3& p1 = p[i];

		switch(sides[i])
		{
		case SIDE_ON:
			f->AddPoint(p1);
			b->AddPoint(p1);
			continue;

		case SIDE_FRONT:
			f->AddPoint(p1);
			break;

		case SIDE_BACK:
			b->AddPoint(p1);
			break;
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
			continue;

	// generate a split point
		const Vector3& p2 = p[(i + 1) % GetNumPoints()];
		Vector3	mid;

		float dot = dists[i] / (dists[i] - dists[i + 1]);
		for (int j = 0 ; j < 3 ; j++)
		{	// avoid round off error when possible
			if (normal[j] == 1)
				mid._e[j] = dist;
			else if (normal[j] == -1)
				mid._e[j] = -dist;
			else
				mid._e[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

		f->AddPoint(mid);
		b->AddPoint(mid);
	}
}

//-----------------------------------------------------------------------
Winding* Winding::ChopInPlace (Winding* in, const Vector3& normal, float dist, float epsilon)
{
	float* dists = ALLOCA(float, in->GetNumPoints() + 1);
	ClipSide* sides = ALLOCA(ClipSide, in->GetNumPoints() + 1);
	int counts[3];

	counts[0] = counts[1] = counts[2] = 0;

// determine sides for each point
	int i;
	for (i = 0; i < in->GetNumPoints(); i++)
	{
		float dot = normal.DotProduct(in->p[i]) - dist;
		dists[i] = dot;
		ClipSide side;
		if (dot > epsilon)
			side = SIDE_FRONT;
		else if (dot < -epsilon)
			side = SIDE_BACK;
		else
		{
			side = SIDE_ON;
		}
		sides[i] = side;
		counts[side]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];
	
	if (!counts[SIDE_FRONT])
	{
		delete in;
		return NULL;
	}
	if (!counts[SIDE_BACK])
		return in;		// in stays the same

	int maxpts = in->GetNumPoints()+4;	// cant use counts[0]+2 because
								// of fp grouping errors

	Winding* f = Alloc(maxpts);

	for (int i = 0 ;i < in->GetNumPoints(); i++)
	{
		const Vector3& p1 = in->p[i];
		
		if (sides[i] == SIDE_ON)
		{
			f->AddPoint(p1);
			continue;
		}
	
		if (sides[i] == SIDE_FRONT)
		{
			f->AddPoint(p1);
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
			continue;
			
	// generate a split point
		const Vector3& p2 = in->p[(i+1)%in->GetNumPoints()];
		Vector3	mid;

		float dot = dists[i] / (dists[i] - dists[i + 1]);
		for (int j = 0 ;j < 3; j++)
		{	// avoid round off error when possible
			if (normal[j] == 1)
				mid._e[j] = dist;
			else if (normal[j] == -1)
				mid._e[j] = -dist;
			else
				mid._e[j] = p1[j] + dot*(p2[j]-p1[j]);
		}
			
		f->AddPoint(mid);
	}

	delete in;
	return f;
}

//-----------------------------------------------------------------------
Winding* Winding::Chop(Winding* in, const Vector3& normal, float dist)
{
	Winding	*f, *b;

	in->ClipEpsilon(normal, dist, ON_EPSILON, &f, &b);
	delete in;
	if (b)
		delete b;
	return f;
}

//-----------------------------------------------------------------------
#define SNAP_EPSILON	0.01

#define Q_rint(in) ((float)floor(in + 0.5))

//-----------------------------------------------------------------------
Vector3 SnapWeldVector(const Vector3& a, const Vector3& b)
{
	int		i;
	float	ai, bi, outi;
	Vector3 out;

	for( i = 0; i < 3; i++ )
	{
		ai = Q_rint( a[ i ] );
		bi = Q_rint( a[ i ] );
		
		if( ai == a[ i ] )
			out._e[ i ] = a[ i ];
		else if( bi == b[ i ] )
			out._e[ i ] = b[ i ];

		else if( fabs((double)(ai - a[ i ]) ) < fabs((double)(bi < b[ i ] )) )
			out._e[ i ] = a[ i ];
		else
			out._e[ i ] = b[ i ];
		
		outi = Q_rint( out[ i ] );
		if( fabs( outi - out[ i ] ) <= SNAP_EPSILON )
			out._e[ i ] = outi;
	}

	return out;
}

//-----------------------------------------------------------------------
bool Winding::Cleanup()
{
	bool		valid = true;
	
	for(int i = 0; i < GetNumPoints(); i++)
	{
		if( GetNumPoints() == 3 )
			return valid;
		
		int j = (i + 1) % GetNumPoints();
		
		// degenerate edge?
		Vector3 vec = p[ i ] - p[ j ];
		
    if( vec.SquareSize() < sqr(DEGENERATE_EPSILON) )
		{
			vec = SnapWeldVector( p[ i ], p[ j ]);
			p[ i ] = vec;
			
			for(int k = i + 2; k < GetNumPoints(); k++ )
			{
				p[ k - 1 ] = p[ k ];
			}
			NumPoints--;
		}
	}
	
	if( GetNumPoints() < 3 )
		valid = false;

	return valid;
}

//-----------------------------------------------------------------------
bool Winding::Check() const
{
	if (GetNumPoints() < 3)
		return false;
	
	float area = Area();
	if (area < 1)
		return false;

	Vector3	facenormal;

	float facedist = Plane(facenormal);
	
	for(int i = 0; i < GetNumPoints(); i++)
	{
		const Vector3& p1 = p[i];

		for (int j = 0; j < 3; j++)
		{
			if (p1[j] > BOGUS_RANGE || p1[j] < BOGUS_RANGE)
				return false;
		}

		int j = i + 1 == GetNumPoints() ? 0 : i + 1;
		
	// check the point is on the face plane
		float d = facenormal.DotProduct(p1) - facedist;
		if (d < -ON_EPSILON || d > ON_EPSILON)
			return false;
	
	// check the edge isn't degenerate
		Vector3 dir = p[j] - p1;

    if (dir.SquareSize() < sqr(ON_EPSILON))
			return false;

    Vector3 edgenormal = (facenormal.CrossProduct(dir)).Normalized();
		
		float edgedist = edgenormal.DotProduct(p1) + ON_EPSILON;
		
	// all other points must be on front side
		for (int j = 0; j < GetNumPoints(); j++)
		{
			if (j == i)
				continue;
			float d = edgenormal.DotProduct(p[j]);
			if (d > edgedist)
				return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------
ClipSide Winding::OnPlaneSide(const Vector3& normal, float dist) const
{
	bool front = false;
	bool back = false;
	for (int i = 0; i < GetNumPoints(); i++)
	{
		float d = normal.DotProduct(p[i]) - dist;
		if (d < -ON_EPSILON)
		{
			if (front)
				return SIDE_CROSS;
			back = true;
			continue;
		}
		if (d > ON_EPSILON)
		{
			if (back)
				return SIDE_CROSS;
			front = true;
			continue;
		}
	}

	if (back)
		return SIDE_BACK;
	
	if (front)
		return SIDE_FRONT;
	
	return SIDE_ON;
}

}

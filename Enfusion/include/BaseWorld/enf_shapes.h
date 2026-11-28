//-----------------------------------------------------------------------------
// File: enf_shapes.h
//
// Desc: Debug visualization library.
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_SHAPES_H
#define ENF_SHAPES_H

#include "El/Math/math3d.hpp"
#include "el/Color/colors.hpp"
#include "common/enf_mathlib.h"
#include "common/enf_mathlib2d.h"

#include "es/Threads/multisync.hpp"
#include "Es/Memory/memAlloc.hpp"
#include <Es/Memory/normalNew.hpp>

#define ENF_SHAPES_DUMMYRENDER


#ifndef ENF_SHAPES_DUMMYRENDER
	#define ENF_SHAPE_CREATE(clone, flags)  Shape::Create(clone, flags);
	#define ENF_SHAPE_CREATEBOX(mins, maxs, color, flags, coll) Shape::CreateBox(mins, maxs, color, flags, coll);
	#define ENF_SHAPE_CREATELINE(p1, p2, color, flags, coll) Shape::CreateLine(p1, p2, color, flags, coll);
	#define ENF_SHAPE_CREATEPOINT(p, size, color, flags, coll) Shape::CreatePoint(p, size, color, flags, coll);
	#define ENF_SHAPE_CREATEPLANE(point, normal, size, color, flags, coll) Shape::CreatePlane(point, normal, size, color, flags, coll);
	#define ENF_SHAPE_CREATELINES(pints, num, color, flags, coll) Shape::CreateLines(pints, num, color, flags, coll);
	#define ENF_SHAPE_CREATECONVEX(planes, stride, num, color, flags, coll) Shape::CreateConvex(planes, stride, num, color, flags, coll);
	#define ENF_SHAPE_CREATESPHERE(pos, radius, color, flags, coll) Shape::CreateSphere(pos, radius, color, flags, coll);
	#define ENF_SHAPE_CREATECYLINDER(pos, rad, len, col, flags, coll) Shape::CreateCylinder(pos, rad, len, col, flags, coll);
	#define ENF_SHAPE_CREATECAPSULE(pos, rad, len, col, flags, coll) Shape::CreateCapsule(pos, rad, len, col, flags, coll);
	#define ENF_SHAPE_CREATEDIAMOND(mins, maxs, col, flags, coll) Shape::CreateDiamond(mins, maxs, col, flags, coll);
	#define ENF_SHAPE_CREATEPYRAMID(mins, maxs, col, flags, coll) Shape::CreatePyramid(mins, maxs, col, flags, coll);
	#define ENF_SHAPE_CREATEMESH(vx, num, color, flags, coll) Shape::CreateMesh(vx, num, color, flags, coll);
	#define ENF_SHAPE_CREATEPOLYGON(vx, num, color, flags, coll) Shape::CreatePolygon(vx, num, color, flags, coll);
	#define ENF_SHAPE_CREATEINDEXMESH(vertices, numVertices, vertexStride, indices, numFaces, indexStride, scale, color, flags, coll) Shape::CreateIndexedMesh(vertices, numVertices, vertexStride, indices, numFaces, indexStride, scale, color, flags, coll);
#else
	#define ENF_SHAPE_CREATE(clone, flags) 0
	#define ENF_SHAPE_CREATEBOX(mins, maxs, color, flags, coll) 0
	#define ENF_SHAPE_CREATELINE(p1, p2, color, flags, coll) 0
	#define ENF_SHAPE_CREATEPOINT(p, size, color, flags, coll) 0
	#define ENF_SHAPE_CREATEPLANE(point, normal, size, color, flags, coll) 0}
	#define ENF_SHAPE_CREATELINES(pints, num, color, flags, coll) 0
	#define ENF_SHAPE_CREATECONVEX(planes, stride, num, color, flags, coll) 0
	#define ENF_SHAPE_CREATESPHERE(pos, radius, color, flags, coll) 0
	#define ENF_SHAPE_CREATECYLINDER(pos, rad, len, col, flags, coll) 0
	#define ENF_SHAPE_CREATECAPSULE(pos, rad, len, col, flags, coll) 0
	#define ENF_SHAPE_CREATEDIAMOND(mins, maxs, col, flags, coll) 0
	#define ENF_SHAPE_CREATEPYRAMID(mins, maxs, col, flags, coll) 0
	#define ENF_SHAPE_CREATEMESH(vx, num, color, flags, coll) 0
	#define ENF_SHAPE_CREATEPOLYGON(vx, num, color, flags, coll) 0
	#define ENF_SHAPE_CREATEINDEXMESH(vertices, numVertices, vertexStride, indices, numFaces, indexStride, scale, color, flags, coll) 0
#endif

class Plane;

namespace enf
{
	/** @addtogroup World
	@{*/

	//struct FColor;
	class Winding;
	struct ShapeMesh;
	class Shape;

	enum ShapeType
	{
		SHAPE_NONE,					//!	When Shape is created and not initialized yet.
		SHAPE_BBOX,					//!	Just box
		SHAPE_LINE,					//!	One or more lines
		SHAPE_BRUSH,				//!	Not implemented
		SHAPE_PIVOT,				//!	3D orientation visualizer
		SHAPE_POINT,				//!	Not implemented
		SHAPE_MESH,					//!	Triangle mesh. Just triangle list, no tri-fan/tri-strip.
		SHAPE_POLYGON,			//!	Polygon
		SHAPE_INDEXEDMESH,	//!	Triangle mesh. Just triangle list, no tri-fan/tri-strip.		
		SHAPE_SPHERE,				//!	Sphere represented by triangle mesh
		SHAPE_CYLINDER,			//!	Cylinder represented by triangle mesh
		SHAPE_DIAMOND,			//! Eight faced pyramid. Defined by bound-box, where middle part is  equal to horizontal extents of box and top/bottom apogees lies on top/bottom side of box.
		SHAPE_PYRAMID,			//! Four sided pyramid. Defined by bound-box, where base is equal to bottom side of box.
		SHAPE_ARROW					//! Arrow pointing down
	};

	enum ShapeFlags
	{
		SS_NOZBUFFER		= BITF(0),				//! Do not compare z-buffer when render
		SS_NOZWRITE			= BITF(1),				//! Do not update z-buffer when render
		SS_WIREFRAME		= BITF(2),				//! Render just wire-frame outline. No solid faces
		SS_TRANSP				= BITF(3),				//! Is translucent
		SS_DOUBLESIDE		= BITF(4),				//! Double-sided (do not cull back-faces)
		SS_ONCE					= BITF(5),				//! Rendered just once and then it's automatically destroyed. Do not keep pointer to these!!
		SS_NOOUTLINE		= BITF(6),				//! Render just solid faces. No wire-frame outline.
		SS_BACKFACE			= BITF(7),				//! Render just back faces
		SS_NOCULL				= BITF(8),				//! Do not cull shapes by view frustum
		SS_VISIBLE			= BITF(9),				//! Make it visible. Set by default
		SS_ADDITIVE			= BITF(10),				//! Additive blending (works with SS_TRANSP)
		SS_NOINSERT			= BITF(20),				//! Do not insert it to shape-collection

		/*!
		internal flags
		*/
		SS_DUMMY				= BITF(28),				//! dummy shape
		SS_PROCESSED		= BITF(29),				//! Internal flag, was processed by render and is ready to be deleted at the beginning of new frame 
		SS_INSTANCED		= BITF(30),
		SS_IDENTMATRIX	= BITF(31)
	};

	//-----------------------------------------------------------------------
	/*!
	ShapeCollection is manager for Shapes. Each ::BaseWorld has created one
	ShapeCollection by default. Shapes are created by static method Shape::Create()
	in current ShapeCollection (See ShapeCollection::SetCurrent())
	*/
	class PUBLIC_API ShapeCollection
	{
		friend class Shape;

	private:
		const static uint INITIAL_SIZE	= 256;

		CriticalSection	m_CriticalSection;
		uint						m_iLastFrame;

		static ShapeCollection*	m_pCurrent;
		Shape**			m_pShapes;
		uint				m_iSize;
		uint				m_iCount;
		uint				m_iFirstFree;
		Ref<Shape>	m_pDummyShape;

		bool Grow();
		void Insert(Shape* sh);
		bool Remove(Shape* sh);

		bool IsValid(Shape* sh) const
		{
			return (sh != reinterpret_cast<Shape*>(~0L) && sh >= reinterpret_cast<Shape*>(MAX_SIZE));
		}
		void SetHWDepthBias(float bias);
		void RenderFirstPass(Shape** firstpass, uint nfirstpass, float bias, int cb);
		void RenderSecondPass(Shape** secondpass, uint nsecondpass, float bias);

	public:
		const static uint MAX_SIZE		= 131072;

		ENF_DECLARE_MMOBJECT(ShapeCollection);

		/*!
		num of shapes in collection
		*/
		ENF_INLINE int GetCount() const
		{
			return m_iCount;
		}

		/*!
		true if collection is full
		*/
		ENF_INLINE int IsFull() const
		{
			return GetCount() >= ShapeCollection::MAX_SIZE - 1;
		}

		/*!
		Renders all shapes in collection. Must be called between Renderer::BeginScene and Renderer::EndScene
		Must not be called manualy when it's ShapeCollection from Scene as it is done automatically
		during CB playing.
		*/
		void RenderShapes();

		//!Removes all m_iFlags & flag shapes
		void FlushShapes(int flag = SS_ONCE);

		/*!
		Sets current ShapeCollection. Shapes created by Shape::Create are put into current
		collection. Keep this in mind when working with shapes!
		*/
		void SetCurrent()
		{
			m_pCurrent = this;
		}

		//!Constructor
		ShapeCollection();

		//!Destroys ShapeCollection and all shapes inside.
		~ShapeCollection();
	};

	//-----------------------------------------------------------------------
	/*!
	Shape is simple visualizer for various geometries. Used for debug purposes and
	for tools See enf::ShapeType for all possible shapes.
	*/
	class PUBLIC_API Shape: public RefCount
	{
		friend class ShapeCollection;

	private:
		//static Class* m_pcType;

		enf::AAB	m_BBox;
		uint			m_Flags;
		ShapeType	m_Type;

		Matrix4	m_Matrix;

		union
		{
			Winding*		m_pWindings;
			ShapeMesh*	m_pMesh;
		};

		uint			m_Color;
		uint			m_OutlineColor;
		ShapeCollection*	m_pCollection;
		int			m_iNum;

		void Render(uint pass) const;
		void Cleanup();

		static Ref<Shape> CreateCylinderCapsule(const Vector3& position, float radius, float length, uint color, uint flags, ShapeCollection* coll, bool cylinder);

		Shape(ShapeCollection* sc);
		~Shape();
	protected:

		void Insert(Winding* wnd);
		static Shape* Create(uint flags = 0, ShapeCollection* coll = NULL);
		virtual void OnUnused() const override
		{
			const_cast<Shape *>(this)->Destroy();
		}

	public:
		ENF_DECLARE_SMALLOBJECT(Shape);
		//static ENF_INLINE Class* Type() { return m_pcType; }

		ENF_INLINE int Compare(const Shape* sh) const
		{
			if(m_Type > sh->m_Type) return 1;
			if(m_Type < sh->m_Type) return -1;
			if(m_Flags > sh->m_Flags) return 1;
			if(m_Flags < sh->m_Flags) return -1;
			if(m_Color > sh->m_Color) return 1;
			if(m_Color < sh->m_Color) return -1;
			return 0;
		}

		ENF_INLINE bool IsDummy()
		{
			return (m_Flags & SS_DUMMY) != 0;
		}

		/*!
		Creates instance of Shape
		*/
		static Ref<Shape> Create(Shape* clone, uint flags);

		static Ref<Shape> CreateBox(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll = NULL);
		static Ref<Shape> CreateLine(const Vector3& p1, const Vector3& p2, uint color, uint flags, ShapeCollection* coll = NULL);
		static Ref<Shape> CreateLine(const Vector2& p1, const Vector2& p2, uint color, uint flags, ShapeCollection* coll = NULL);
		static Ref<Shape> CreatePoint(const Vector3& p, float size, uint color, uint flags, ShapeCollection* coll = NULL);
		static Ref<Shape> CreatePlane(const Vector3& point, const Vector3& normal, float size, uint color, uint flags, ShapeCollection* coll = NULL);
		/*!
		Create list of lines
		\param points Array of points
		\param num Number of points. Must be multiply of 2
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreateLines(const Vector3* points, uint num, uint color, uint flags, ShapeCollection* coll = NULL);
		/*!
		Create convex polyhedron
		\param planes Array of planes, defining convex shape
		\param stride from one plane to next one
		\param num Number of planes
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreateConvex(const Vector4* planes, uint stride, uint num, uint color, uint flags, ShapeCollection* coll = NULL);
		/*!
		Create sphere
		\param position Position of shape
		\param radius Radius of sphere
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreateSphere(const Vector3& position, float radius, uint color, uint flags, ShapeCollection* coll = NULL);
		/*!
		Create cylinder aligned with Z axis
		\param position Position of cylinder
		\param radius Radius of cylinder
		\param length Length of cylinder
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreateCylinder(const Vector3& position, float radius, float length, uint color, uint flags, ShapeCollection* coll = NULL)
		{
			return CreateCylinderCapsule(position, radius, length, color, flags, coll, true);
		}
		static Ref<Shape> CreateCapsule(const Vector3& position, float radius, float length, uint color, uint flags, ShapeCollection* coll = NULL)
		{
			return CreateCylinderCapsule(position, radius, length, color, flags, coll, false);
		}
		static Ref<Shape> CreateDiamond(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll = NULL);
		static Ref<Shape> CreatePyramid(const Vector3& mins, const Vector3& maxs, uint color, uint flags, ShapeCollection* coll = NULL);
		/*!
		Create triangle-list mesh.
		\param vertexes Array of vertexes
		\param num Number of triangles
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreateMesh(const Vector3* vertexes, uint num, uint color, uint flags, ShapeCollection* coll = NULL);

		/*!
		Create n-point polygon
		\param vertexes Array of vertexes
		\param num Number of triangles
		\param color Color encoded in A8R8G8B8 form.
		\param flags Flags, see enf::ShapeFlags
		*/
		static Ref<Shape> CreatePolygon(const Vector3* vertexes, uint num, uint color, uint flags, ShapeCollection* coll = NULL);

		static Ref<Shape> CreateIndexedMesh(const Vector3* vertexes, uint numVertexes, uint vertexStride, const ushort* indexes, uint numFaces, uint indexStride, float scale, uint color, uint flags, ShapeCollection* coll = NULL);

		//!Returns current AAB of shape
		const AAB& GetAABB() const
		{
			return m_BBox;
		}

		/*!
		Adds mesh into Shape. Valid only for those created by enf::Shape::CreateMesh
		\param vertexes Array of vertexes
		\param num Number of triangles
		*/
		void AddMesh(const Vector3* vertexes, uint num);

		/*!
		Adds polygon into Shape. Valid only for those created by enf::Shape::CreatePolygon
		\param vertexes Array of vertexes
		\param num Number of edges
		*/
		void AddPolygon(const Vector3* vertexes, uint num);

		/*!
		Sets color for Shape.
		\param color Color encoded in A8R8G8B8 form.
		*/
		void NMETHOD SetColor(uint color)
		{
			m_Color = color;
		}

		/*!
		Sets color for Shape.
		\param color Color in FColor type, ranged from 0 to 1.
		*/
		void SetFColor(const Color& color)
		{
			m_Color = PackedColor(color);
		}

		/*!
		Sets outline color for Shape.
		\param color Color encoded in A8R8G8B8 form.
		*/
		void NMETHOD SetOutlineColor(uint color)
		{
			m_OutlineColor = color;
		}

		/*!
		Sets outline color for Shape.
		\param color Color in FColor type, ranged from 0 to 1.
		*/
		void SetOutlineFColor(const Color& color)
		{
			m_OutlineColor = PackedColor(color);
		}

		/*!
		Sets flags for this Shape. See enf::ShapeFlags
		\param flags Flags to be set. They are or'ed with previous ones.
		*/
		void NMETHOD SetFlags(uint flags)
		{
			m_Flags |= flags & 0x00ffffff;
		}

		/*!
		Clears flags for this Shape. See enf::ShapeFlags
		\param flags Flags to be cleared.
		*/
		void NMETHOD ClearFlags(uint flags)
		{
			m_Flags &= ~(flags & 0x00ffffff);
		}

		/*!
		Sets orientation matrix of shape.
		\param mat Matrix
		*/
		void NMETHOD SetMatrix(Matrix4Par mat)
		{
			m_Matrix = mat;
			if(mat.IsIdentity())
				m_Flags |= SS_IDENTMATRIX;
			else
				m_Flags &= ~SS_IDENTMATRIX;
		}

		//! Destroys the Shape
		void NMETHOD Destroy()
		{
			delete this;
		}
	};
	//@}
}
#endif //ENF_SHAPES_H

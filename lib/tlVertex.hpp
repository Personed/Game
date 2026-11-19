#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TLVERTEX_HPP
#define _TLVERTEX_HPP

#include "Shape/vertex.hpp"
#include <El/Color/colors.hpp>
#include <El/Math/math3dP.hpp>
#include <Es/Containers/staticArray.hpp>

class LightList;

typedef PackedColor TLColor;

//typedef Color TLColor; // old implementation

class TLVertex
{
	public:
	Vector3P pos; // D3DFVF_XYZRHW
	float rhw;
  PackedColor color; // D3DFVF_DIFFUSE
  PackedColor specular; // D3DFVF_SPECULAR
  UVPair t0; // D3DFVF_TEXCOORDSIZE2(0) 
  UVPair t1; // D3DFVF_TEXCOORDSIZE2(1) 
	#if _PIII
	UVPair t2; // force alignment to 16B multiply
	#endif
};

TypeIsSimple(TLVertex)


class TLVertexTable
{
	// this class stores transformed and lit vetices
	// is uses view cordinate system
	
	protected:
	ClipFlags _orHints,_andHints; // we can do some optimizations based on this

	private:
	StaticArray<Vector3> _posTrans;
	StaticArray<TLVertex> _vert;
	StaticArray<ClipFlags> _clip; // TODO: where is stored clip in D3D?
	
	public:
	// initializers
	void ReleaseTables();

	private:
	// disable default copy
	TLVertexTable( const TLVertexTable &src );
	void operator = ( const TLVertexTable &src );

	public:
	
	// constructors
	TLVertexTable();
	void DoConstruct();

	~TLVertexTable() {ReleaseTables();}

	// properties
	int NVertex() const {return _vert.Size();}

	int AddPos();

	Vector3Val TransPosA( int i ) const {return _posTrans[i];}
	void SetTransPosA( int i, const Vector3 &val ) {_posTrans[i]=val;}


	Vector3Val TransPos( int i ) const {return _posTrans[i];}
	Vector3 &SetTransPos( int i ) {return _posTrans[i];}

	Vector3PVal ScreenPos( int i ) const {return _vert[i].pos;}
	Vector3P &SetScreenPos( int i ) {return _vert[i].pos;}

	ClipFlags Clip( int i ) const {return _clip[i];}
	void SetClip( int i, ClipFlags clipped ) {_clip[i]=clipped;}

	const TLVertex &GetVertex( int i ) const {return _vert[i];}
	TLVertex &SetVertex( int i ) {return _vert[i];}

	const TLVertex *VertexData() const {return _vert.Data();}
	TLVertex *VertexData() {return _vert.Data();}

	float U( int i ) const {return _vert[i].t0.u;}
	float V( int i ) const {return _vert[i].t0.v;}
	void SetU( int i, float u ) {_vert[i].t0.u=u;}
	void SetV( int i, float v ) {_vert[i].t0.v=v;}

	const UVPair &UV( int i ) const {return _vert[i].t0;}
	void SetUV( int i, float u, float v ){_vert[i].t0.u=u,_vert[i].t0.v=v;}

	//const TLColor *GetColorData() const {return _color.Data();}
	const TLColor &GetColor( int i ) const {return _vert[i].color;}
	TLColor &SetColor( int i ) {return _vert[i].color;}

	const TLColor &GetSpecular( int i ) const {return _vert[i].specular;}
	TLColor &SetSpecular( int i ) {return _vert[i].specular;}

	// methods
	void DoTransformPoints(const VertexTableAnimationContext *animContext, const Shape &src, const Matrix4 &posView);
	void DoTransformPoints(const Array<Vector3> &srcPos, const Matrix4 &posView, int beg, int end);

	// transformation constructor
	TLVertexTable(const VertexTableAnimationContext *animContext, const Shape &src, const Matrix4 &toView);
	void Init(const VertexTableAnimationContext *animContext, const Shape &src, const Matrix4 &toView);
	ClipFlags CheckClipping( const Camera &camera, ClipFlags mask, ClipFlags &andClip );
	void DoPerspective( const Camera &camera, ClipFlags clip );

	ClipFlags GetOrHints() const {return _orHints;}
	ClipFlags GetAndHints() const {return _andHints;}

	// custom lighting
	void DoLighting(
		ColorVal animColor,
		const Matrix4 &worldToModel, const LightList &lights,
		const Shape &mesh, int spec
	);

	void DoMaterialLightingP(
		const TLMaterial &mat,
		const Matrix4 &worldToModel, const LightList &lights,
		const VertexTable &mesh, int beg, int end
	);
	void DoLineLighting(const VertexTable &mesh, int spec);

};

#endif


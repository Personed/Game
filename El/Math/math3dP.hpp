#ifdef _MSC_VER
#pragma once
#endif

#ifndef _MATH3DP_HPP
#define _MATH3DP_HPP

// homogenous vector and matrix arithmetics

#include <Es/Containers/array.hpp>

/// select between template and straightforward implementation
//#define _T_MATH 1

#ifdef _T_MATH

// use template version


#include "math3DT.hpp"

// pretend T types are P types (they have same binary format)
#define Vector3P Vector3T
#define Vector3PVal Vector3TVal
#define Vector3PPar Vector3TPar
#define Matrix3P Matrix3T
#define Matrix4P Matrix4T
#define Matrix3PVal const Matrix3T &
#define Matrix4PVal const Matrix4T &
#define Matrix3PPar const Matrix3T &
#define Matrix4PPar const Matrix4T &
#define ConvertToM(x) (x)
#define ConvertToP(x) (x)
#define VZeroP VZeroT
#define VUpP VUpT
#define VForwardP VForwardT
#define VAsideP VAsideT

#define MIdentityP M4IdentityT
#define MZeroP M4ZeroT
#define M4IdentityP M4IdentityT
#define M4ZeroP M4ZeroT
#define M3IdentityP M3IdentityT
#define M3ZeroP M3ZeroT

#else

// (C) 1997, SUMA

// straightforward C++ version
#include "mathDefs.hpp"

#include <math.h>
#include <float.h>
#include <Es/Common/fltopts.hpp>

#include "mathOpt.hpp"

// use parameter placeholder to explicitly disable initialization of elements

#ifndef vecAlign
  // no alignment required in P implementation
  #define vecAlign
#endif

// define how should const references be passed
#define Vector3PPar const Vector3P &
#define Vector3PVal const Vector3P &

#define Matrix3PPar const Matrix3P &
#define Matrix3PVal const Matrix3P &

#define Matrix4PPar const Matrix4P &
#define Matrix4PVal const Matrix4P &

class Vector3P;
class Matrix3P;
class Matrix4P;

extern const Vector3P VZeroP;
extern const Vector3P VUpP;
extern const Vector3P VForwardP;
extern const Vector3P VAsideP;

extern const Matrix3P M3ZeroP;
extern const Matrix3P M3IdentityP;

extern const Matrix4P M4ZeroP;
extern const Matrix4P M4IdentityP;

#include <Es/Framework/debugLog.hpp>
#include <Es/Containers/typeOpts.hpp>

#if _MSC_VER
#define NAKED __declspec( naked )
#else
#define NAKED
#endif


namespace enf
{
	struct MPlane;
}


//-----------------------------------------------------------------------
/*!
This class represents a vector in 3D space.
*/
//-----------------------------------------------------------------------
class Vector3P
{
  friend class Matrix4P;
  friend class Matrix3P;
  
  // 3D type - used for rendering, screen clipping ...
  public:
    union //TODO: temporary solution to make Arma/Enforce vector compatible.
    {
      //! Vector of three coordinates with XYZ order.
      Coord _e[3];
      float v[3];				///< array of three floats

      struct
      {
        float x, y, z;		///< x, y, z of the vector
      };
    };
  //! Getting of the specified coordinate.
  /*!
    This method returns a non l-value.
    \param i Index of coordinate to retrieve. 0 = X, 1 = Y, 2 = Z.
    \return Coordinate itself.
  */
  __forceinline Coord Get( int i ) const {return _e[i];}
  //! Setting of the specified coordinate.
  /*!
    This method returns a l-value.
    \param i Index of coordinate to set. 0 = X, 1 = Y, 2 = Z.
    \return Coordinate itself.
  */
  __forceinline Coord &Set( int i ) {return _e[i];}
  
  public:
  __forceinline bool IsZero() const
	{
		return (_e[0] == 0 && _e[1] == 0 && _e[2] == 0);
	}

  // data initializers
  //! Initialization of a vector by transformation of the specified vector by the specified matrix.
  /*!
    \param v Source vector.
    \param a Matrix to transform source vector by.
  */
  void SetMultiplyLeft( Vector3PPar v, const Matrix3P &a );
  //! Initialization of a vector by transformation of the specified vector by the specified matrix.
  /*!
    Only 3x3 sub-matrix of the specified matrix is used.
    \param a Matrix to transform source vector by.
    \param v Source vector.
  */
  void SetRotate( const Matrix4P &a, Vector3PPar v );
  //! Initialization of a vector by transformation of the specified vector by the specified matrix.
  /*!
    This function is coded in assembly.
    Only 3x3 sub-matrix of the specified matrix is used.
    \param a Matrix to transform source vector by.
    \param v Source vector.
  */
  void SetFastTransform( const Matrix4P &a, Vector3PPar v );

  //! Inline version of the function SetFastTransform.
  /*!
    \param a Matrix to transform source vector by.
    \param v Source vector.
  */
  __forceinline void SetMultiply( const Matrix3P &a, Vector3PPar v );
  //! Inline version of the function SetFastTransform.
  /*!
    \param a Matrix to transform source vector by.
    \param v Source vector.
  */
  __forceinline void SetMultiply( const Matrix4P &a, Vector3PPar v );
  // ???
  /*!
  */
  float SetPerspectiveProject( const Matrix4P &a, Vector3PPar o );
  

  // constructors
  #if !_RELEASE || _PROFILE
    Vector3P(){_e[0]=_e[1]=_e[2]=FLT_SNAN;} // default no init
  #else
    __forceinline Vector3P(){}
  #endif
  __forceinline void Init() {} // init 4th component
  //__forceinline Vector3P( enum _vZero ){_e[0]=_e[1]=_e[2]=coord(0);}
  //! Constructor which accepts 3 coordinates.
  /*!
    \param x X coordinate.
    \param y Y coordinate.
    \param z Z coordinate.
  */
  __forceinline Vector3P( Coord x, Coord y, Coord z )
  {
    _e[0]=x,_e[1]=y,_e[2]=z;
  }
  //! Copy constructor.
  /*!
    Note that compiler generated default copy is not inline.
    \param src Source vector.
  */
  __forceinline Vector3P( const Vector3P &src )
  {
    _e[0]=src._e[0],_e[1]=src._e[1],_e[2]=src._e[2];
  }
	__forceinline Vector3P &operator =( const Vector3P &src )
	{
		// copying uninitialized data is not a problem - problem is using them
		_e[0]=src._e[0],_e[1]=src._e[1],_e[2]=src._e[2];
		return *this;
	}

	__forceinline Vector3P &operator =( const float* src )
	{
		_e[0]=src[0],_e[1]=src[1],_e[2]=src[2];
		return *this;
	}

  __forceinline Vector3P( enum _noInit ){}
  __forceinline explicit Vector3P( const float* val){_e[0]= val[0], _e[1]= val[1]; _e[2]= val[2];}
  __forceinline explicit Vector3P( float val){_e[0]=_e[1]=_e[2]= val;}
  __forceinline Vector3P( enum _guardInit ){_e[0]=_e[1]=_e[2]=FLT_SNAN;}

  __forceinline Vector3P( enum _vMultiply, const Matrix3P &a, Vector3PPar v ){SetMultiply(a,v);}
  __forceinline Vector3P( enum _vMultiply, const Matrix4P &a, Vector3PPar v ){SetMultiply(a,v);}
  __forceinline Vector3P( enum _vMultiplyLeft, Vector3PPar v, const Matrix3P &a ){SetMultiplyLeft(v,a);}

  __forceinline Vector3P( enum _vRotate, const Matrix4P &a, Vector3PPar v ){SetRotate(a,v);}
  __forceinline Vector3P( enum _vFastTransform, const Matrix4P &a, Vector3PPar v ){SetFastTransform(a,v);}
  __forceinline Vector3P( enum _vFastTransformA, const Matrix4P &a, Vector3PPar v ){SetFastTransform(a,v);}
  //Vector3P( enum _vPerspective, const Matrix4P &a, Vector3PPar v ){SetPerspectiveProject(a,v);}
  
  // properties
  __forceinline Coord operator []( int i ) const {return _e[i];}
  __forceinline Coord &operator [] ( int i ) {return _e[i];}
  
  __forceinline Coord X() const {return _e[0];}
  __forceinline Coord Y() const {return _e[1];}
  __forceinline Coord Z() const {return _e[2];}

  const Coord *GetArray() const {return _e;}
  
  Coord SquareSize() const {return X()*X()+Y()*Y()+Z()*Z();}
  __forceinline Coord SquareSizeInline() const {return X()*X()+Y()*Y()+Z()*Z();}
  Coord Size() const
  {
    //return sqrt(X()*X()+Y()*Y()+Z()*Z());}
    // optimization: |v|=|v|*|v|/|v|
    float size2=SquareSizeInline();
    if( size2<=FLT_MIN ) return 0;
    if (size2>FLT_MAX) return size2; // >FLT_MAX means INFINITY
    Coord invSize=InvSqrt(size2);
    return size2*invSize;
  }
  Coord InvSize() const {return InvSqrt(SquareSize());}
  Coord InvSquareSize() const {return Inv(SquareSize());}

  Coord SquareSizeXZ() const {return X()*X()+Z()*Z();}
  Coord SizeXZ() const
  {
    float size2=SquareSizeXZ();
    if( size2<=FLT_MIN  ) return 0;
    Coord invSize=InvSqrt(size2);
    return size2*invSize;
  }
  Coord InvSizeXZ() const {return InvSqrt(SquareSizeXZ());}
  Coord InvSquareSizeXZ() const {return Inv(SquareSizeXZ());}
  
  // vector arithmetics
  __forceinline Vector3P operator - () const
  {
    return Vector3P(-X(),-Y(),-Z());
  }
  
  __forceinline Vector3P operator + ( Vector3PPar op ) const
  {
    return Vector3P(X()+op.X(),Y()+op.Y(),Z()+op.Z());
  }
  __forceinline Vector3P operator - ( Vector3PPar op ) const
  {
    return Vector3P(X()-op.X(),Y()-op.Y(),Z()-op.Z());
  }
  __forceinline friend Vector3P operator * ( Coord f, Vector3PPar op );
  __forceinline Vector3P operator * ( Coord f ) const
  {
    return Vector3P(Get(0)*f,Get(1)*f,Get(2)*f);
  }
  __forceinline Vector3P Modulate( Vector3PPar op ) const
  {
    return Vector3P(Get(0)*op.Get(0),Get(1)*op.Get(1),Get(2)*op.Get(2));
  }
  Vector3P operator / ( Coord f ) const
  {
    float invF=1/f;
    return Vector3P(X()*invF,Y()*invF,Z()*invF);
  }
  
  Vector3P &operator += ( Vector3PPar op )
  {
    _e[0]+=op.X();
    _e[1]+=op.Y();
    _e[2]+=op.Z();
    return *this;
  }
  Vector3P &operator -= ( Vector3PPar op )
  {
    _e[0]-=op.X();
    _e[1]-=op.Y();
    _e[2]-=op.Z();
    return *this;
  }
  Vector3P &operator *= ( Coord f )
  {
    _e[0]*=f;
    _e[1]*=f;
    _e[2]*=f;
    return *this;
  }
  Vector3P &operator /= ( Coord f )
  {
    float invF=1/f;
    _e[0]*=invF;
    _e[1]*=invF;
    _e[2]*=invF;
    return *this;
  }
  
  Vector3P Normalized() const;
  inline Vector3P NormalizedFast() const;
  float Normalize(); // return size//no return to avoid using instead of Normalized
  Coord NormalizeSize();

  __forceinline Coord DotProduct( Vector3PPar op ) const
  {
    return X()*op.X()+Y()*op.Y()+Z()*op.Z();
  }
  __forceinline Coord operator * ( Vector3PPar op ) const
  {
    return DotProduct(op);
  }
  __forceinline Vector3P Multiply (Vector3PPar op) const
  {
    return Vector3P(X()*op.X(), Y()*op.Y(), Z()*op.Z());
  }
  __forceinline Vector3P operator * ( const Matrix3P &op ) const
  {
    return Vector3P(VMultiplyLeft,*this,op);
  }

  ///Calculates cosinus angle between this vector and another vector
  /**
   * @param other vector
   * @return cosinus of angle. Use arccos to get angle in range between 0 .. PI
   */
  float CosAngle( Vector3PPar op ) const;

  ///Calculates sinus angle between this vector and another vector
  /**
   * @param other vector
   * @param worldsTop To define quadrant for negative values, you have to declare vector
   *  that representing "worlds top" direction. In other words, you have to define, which
   *  direction is up, and which direction is bottom. This also defines rotation direction
   *  using the three finger rule (left hand rule or right hand rule). The vector
   *  doesn't need to normalized and doesn't need to be upright, 
   *  bud result vector of cross product (is upright to
   *  both vectors) needn't be upright to worlds top. In such a case, 
   *  result is always positive
   * @return sinus of angle. Use arcsin to get angle in range between -PI/2 .. PI/2
   */
  float SinAngle( Vector3PPar op, Vector3PPar worldsTop ) const;
  float Distance( Vector3PPar op ) const;
  __forceinline float Distance2( Vector3PPar op ) const
  {
    return (*this-op).SquareSizeInline();
  }
  __forceinline float Distance2Inline( Vector3PPar op ) const
  {
    return (*this-op).SquareSizeInline();
  }

  float DistanceXZ( Vector3PPar op ) const
  {
    return (*this-op).SizeXZ();
  }
  float DistanceXZ2( Vector3PPar op ) const
  {
    return (*this-op).SquareSizeXZ();
  }

  /*!
	Linear interpolation between two vectors, lerp == 0 -> return this
	\param vec		second vector3 for interpolation as reference
	\param lerp		parameter
	\return interpolated vector
	*/
	__forceinline Vector3P Lerp(Vector3PPar vec, float lerp) const
	{
		float lx = _e[0], ly = _e[1], lz = _e[2];
		return Vector3P((vec._e[0] - lx) * lerp + lx, (vec._e[1] - ly) * lerp + ly, (vec._e[2] - lz) * lerp + lz);
	}


  Vector3P Project( Vector3PPar op ) const;
  __forceinline Vector3P CrossProduct( Vector3PPar op ) const
  {
	  float ox = op.X();
	  float oy = op.Y();
	  float oz = op.Z();
	  float lx = X();
	  float ly = Y();
	  float lz = Z();

	  float x=ly*oz-lz*oy;
	  float y=lz*ox-lx*oz;
	  float z=lx*oy-ly*ox;
	  return Vector3P(x,y,z);
  }
  Matrix3P Tilda() const;
  bool IsFinite() const;

  bool operator == ( Vector3PPar cmp ) const {return cmp.X()==X() && cmp.Y()==Y() && cmp.Z()==Z();}
  bool operator != ( Vector3PPar cmp ) const {return cmp.X()!=X() || cmp.Y()!=Y() || cmp.Z()!=Z();}

  /**
   * Solves homogen. equation system, returns reliability of the result
   *  (higher values are better) or -1 if not solvable.
   */
  Coord SetSolve2x2 ( Coord a, Coord b, Coord c, Coord d, Coord e, Coord f );

  /**
   * Finds eigenvector to the given eigenvalue ("a" to "f" are items
   *  of symmetric matrix to be solved). Returns reliability (higher is better)
   *  or -1 if not solvable.
   */
  Coord SetEigenVector ( Coord a, Coord b, Coord c, Coord d, Coord e, Coord f );

  /// Returns true for [1,1,1]
  bool AllOnes () const;

  bool AllZeroes() const 
  {
    return _e[0] == 0.0f && _e[1] == 0.0f && _e[2] == 0.0f;
  }
  const Vector3P& Zero()
  {
    _e[0] = 0.0f;
    _e[1] = 0.0f;
    _e[2] = 0.0f;
    return *this;
  }

	//ENF added to be compatible
	__forceinline float SquaredDistance(Vector3PPar op) const 
	{
		return (op - *this).SquareSizeInline();
	}

	__forceinline float Dot(Vector3PPar op) const
	{
		return DotProduct(op);
	}

	__forceinline float Length() const
	{
		return Size();
	}
};

TypeIsSimple(Vector3P);

//-----------------------------------------------------------------------
__forceinline Vector3P operator * ( Coord f, Vector3PPar op )
{
  return Vector3P(op.Get(0)*f,op.Get(1)*f,op.Get(2)*f);
}


namespace enf
{
  class AAB;
}

void SetRollPitchYawScale(Matrix3P& m, float roll, float pitch, float yaw, float scale);

//-----------------------------------------------------------------------
class Matrix3P
{
  friend class Vector3P;
  friend class Matrix4P;
  friend class enf::AAB;
  
  // homogeneous matrix - transformations
  private:
  //Vector3P _columns[3];
  //__forceinline Coord Get( int i, int j ) const {return _columns[j][i];}
  //__forceinline Coord &Set( int i, int j ) {return _columns[j][i];}

  // non-array implementation
	union
  {
		struct 
		{
			Vector3P _aside;
			Vector3P _up;
			Vector3P _dir;
		};

		float	m[3][3];
		struct
		{
			float _11, _12, _13;
			float	_21, _22, _23;
			float	_31, _32, _33;
		};
	};
  __forceinline Coord Get( int i, int j ) const {return (&_aside)[j][i];}
  __forceinline Coord &Set( int i, int j ) {return (&_aside)[j][i];}
  
  public:
  // functions that load matrix with data
  // used internally in constructors, but may be useful also to other purpose
  void SetIdentity(); 
  void SetZero();
  //! Sets the rotation matrix according to the X axis
  void SetRotationX( Coord angle );
  //! Sets the rotation matrix according to the Y axis
  /*!
    Note that this method is not in consistency with the other 2 methods.
    The angle here is of the opposite meaning.
  */
  void SetRotationY( Coord angle );
  //! Sets the rotation matrix according to the Z axis
  void SetRotationZ( Coord angle );
  //! Sets the rotation matrix according to an arbitrary axis
  void SetRotationAxis(Vector3PPar axis, Coord angle);
  
  void SetScale( Coord x, Coord y, Coord z );

  void SetScale( float scale );
  /// get squared average scale (some vectors can be after transformation longer)
  float Scale2() const;
  /// get squared maximal scale
  float MaxScale2() const;
  // optimization calculate scale and maxscale at once
  void ScaleAndMaxScale2(float& scale, float& maxScale) const;
  
  /// get average scale  (some vectors can be after transformation longer)
  float Scale() const
  {
    // if scale is zero, the matrix is singular
    float scale2=Scale2();
    Assert(scale2>=0);
    return sqrt(scale2);
  }
  /// get maximal scale
  float MaxScale() const
  {
    // if scale is zero, the matrix is singular
    float scale2=MaxScale2();
    Assert(scale2>=0);
    return sqrt(scale2);
  }
  void ScaleAndMaxScale(float& scale, float& maxScale) const
  {
    ScaleAndMaxScale2(scale, maxScale);
    Assert(scale>=0 && maxScale>=0);
    scale = sqrt(scale);
    maxScale = sqrt(maxScale);
  }

  /// get inverse scale
  float InvScale() const
  {
    float scale2=Scale2();
    if( scale2<=0 ) return 1; // singular matrix
    return InvSqrt(scale2);
  }

  //! Setting the orientation of the matrix according to direction and up vectors.
  /*!
    Note that axis Z is the reference axis for the directional vector and Y is
    the reference axis for the up vector. By this I mean that if you set those
    vectors to dir = (0,0,1) and up = (0,1,0), the matrix will be identity.
    After calling this function the matrix will be orthogonalized. The direction
    will remain unchanged whereas the up vector will be modified to be orthogonal
    to the direction vector.
    \param dir Directional vector.
    \param up Up vector.
    \sa SetUpAndAside, SetUpAndDirection, SetDirectionAndAside
  */
  void SetDirectionAndUp( Vector3PPar dir, Vector3PPar up ); // sets only 3x3 sub-matrix
  //! Setting the orientation of the matrix according to up and aside vectors.
  /*!
    Up vector will remain unchanged, aside vector will be modified to be orthogonal
    to the up vector.
    \param up Up vector.
    \param aside Aside vector.
    \sa SetDirectionAndUp, SetUpAndDirection, SetDirectionAndAside
  */
  void SetUpAndAside( Vector3PPar up, Vector3PPar aside );
  //! Setting the orientation of the matrix according to up and direction vectors.
  /*!
    Up vector will remain unchanged, dir vector will be modified to be orthogonal
    to the up vector.
    \param up Up vector.
    \param dir Directional vector.
    \sa SetDirectionAndUp, SetUpAndAside, SetDirectionAndAside
  */
  void SetUpAndDirection( Vector3PPar up, Vector3PPar dir );
  //! Setting the orientation of the matrix according to direction and aside vectors.
  /*!
    Directional vector will remain unchanged, aside vector will be modified to be orthogonal
    to the directional vector.
    \param dir Directional vector.
    \param aside Aside vector.
    \sa SetDirectionAndUp, SetUpAndAside, SetUpAndDirection
  */
  void SetDirectionAndAside( Vector3PPar dir, Vector3PPar aside );

  __forceinline void InlineSetMultiply( const Matrix3P &a, float op )
  {
    SetMultiply(a,op);
  }
  __forceinline void InlineAddMultiply( const Matrix3P &a, float op )
  {
    AddMultiply(a,op);
  }
  __forceinline void InlineSetAdd( const Matrix3P &a, const Matrix3P &b )
  {
    SetAdd(a,b);
  }

  void AddMultiply( const Matrix3P &a, float op );
  void SetMultiply( const Matrix3P &a, const Matrix3P &b )
  {
    float x,y,z;
    x=b.Get(0,0);
    y=b.Get(1,0);
    z=b.Get(2,0);
    Set(0,0)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,0)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,0)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);

    x=b.Get(0,1);
    y=b.Get(1,1);
    z=b.Get(2,1);
    Set(0,1)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,1)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,1)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);

    x=b.Get(0,2);
    y=b.Get(1,2);
    z=b.Get(2,2);
    Set(0,2)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,2)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,2)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);
  }
  void SetAdd( const Matrix3P &a, const Matrix3P &b );
  void SetMultiply( const Matrix3P &a, float op );
  void SetInvertRotation( const Matrix3P &op );
  void SetInvertScaled( const Matrix3P &op );
  void SetInvertGeneral( const Matrix3P &op );
  void SetNormalTransform( const Matrix3P &op );
  void SetTilda( Vector3PPar a );
  
  // placeholder parameter describes constructor type
  Matrix3P
  (
    float m00, float m01, float m02,
    float m10, float m11, float m12,
    float m20, float m21, float m22
  )
  {
    Set(0,0)=m00,Set(0,1)=m01,Set(0,2)=m02;
    Set(1,0)=m10,Set(1,1)=m11,Set(1,2)=m12;
    Set(2,0)=m20,Set(2,1)=m21,Set(2,2)=m22;
  }
  __forceinline Matrix3P(){}
  __forceinline Matrix3P(Vector3PPar aside, Vector3PPar up, Vector3PPar dir):_dir(dir), _aside(aside), _up(up){}
  __forceinline Matrix3P( enum _noInit ){}
  __forceinline Matrix3P( enum _guardInit ):_aside(GuardInit),_up(GuardInit),_dir(GuardInit){}
  //__forceinline Matrix3P( enum _mIdentity ){SetIdentity();}
  //__forceinline Matrix3P( enum _mZero ){SetZero();}
  __forceinline Matrix3P( enum _mRotationX, Coord angle ){SetRotationX(angle);}
  __forceinline Matrix3P( enum _mRotationY, Coord angle ){SetRotationY(angle);}
  __forceinline Matrix3P( enum _mRotationZ, Coord angle ){SetRotationZ(angle);}
  __forceinline Matrix3P( enum _mRotationAxis, Vector3PPar axis, Coord angle ) {SetRotationAxis(axis,angle);}
  __forceinline Matrix3P( enum _mScale, Coord x, Coord y, Coord z ){SetScale(x,y,z);}
  __forceinline Matrix3P( enum _mScale, Coord x ){SetScale(x,x,x);}
  __forceinline Matrix3P( enum _mDirection, Vector3PPar dir, Vector3PPar up )
  {
    SetDirectionAndUp(dir,up);
  }
  __forceinline Matrix3P( enum _mUpAndDirection, Vector3PPar up, Vector3PPar dir )
  {
    SetUpAndDirection(up,dir);
  }
  __forceinline Matrix3P( enum _mMultiply, const Matrix3P &a, const Matrix3P &b ){SetMultiply(a,b);}
  __forceinline Matrix3P( enum _mMultiply, const Matrix3P &a, float op ){SetMultiply(a,op);}
  __forceinline Matrix3P( enum _mInverseRotation, const Matrix3P &a ){SetInvertRotation(a);}
  __forceinline Matrix3P( enum _mInverseGeneral, const Matrix3P &a ){SetInvertGeneral(a);}
  __forceinline Matrix3P( enum _mInverseScaled, const Matrix3P &a ){SetInvertScaled(a);}
  __forceinline Matrix3P( enum _mNormalTransform, const Matrix3P &a ){SetNormalTransform(a);}
  __forceinline Matrix3P( enum _mTilda, Vector3PPar a ){SetTilda(a);}
  
  // following operators are defined so that no copy constuctor is used
  // if they are expanded inline, copy is not needed
  __forceinline Matrix3P operator * ( const Matrix3P &op ) const
  {
    // matrix multiplication
    return Matrix3P(MMultiply,*this,op);
  }
  __forceinline Vector3P operator * ( Vector3PPar op ) const
  {
    // vector transformation
    return Vector3P(VMultiply,*this,op);
  }
  //kvuli citelnosti
  __forceinline Vector3P TransformVector ( Vector3PPar op ) const
  {
    return Vector3P(VMultiply,*this,op);
  }
  __forceinline Vector3P InvTransformVector ( Vector3PPar op ) const
  {
    Matrix3P trans(_aside._e[0], _up._e[0], _dir._e[0],
                  _aside._e[1], _up._e[1], _dir._e[1],
                  _aside._e[2], _up._e[2], _dir._e[2]);
    return trans.TransformVector(op);
  }
  __forceinline Matrix3P operator * ( float op ) const
  {
    return Matrix3P(MMultiply,*this,op);
  }
  void operator *= ( float op );
  Matrix3P operator + ( const Matrix3P &a ) const;
  Matrix3P operator - ( const Matrix3P &a ) const;
  Matrix3P &operator += ( const Matrix3P &a );
  Matrix3P &operator -= ( const Matrix3P &a );    
  Matrix3P operator -() const
  {
    Matrix3P ret;
    ret._aside = -_aside;
    ret._up = -_up;
    ret._dir = -_dir;
    return ret;
  }

  bool operator ==(const Matrix3P &op) const {return _aside==op._aside&& _up==op._up && _dir==op._dir;}
  bool operator !=(const Matrix3P &op) const {return _aside!=op._aside || _up!=op._up || _dir!=op._dir;}
  
  __forceinline Matrix3P InverseRotation() const {return Matrix3P(MInverseRotation,*this);}
  __forceinline Matrix3P InverseGeneral() const {return Matrix3P(MInverseGeneral,*this);}
  __forceinline Matrix3P InverseScaled() const {return Matrix3P(MInverseScaled,*this);}
  __forceinline Matrix3P NormalTransform() const {return Matrix3P(MNormalTransform,*this);}
  // member access operators
  __forceinline Coord operator () ( int i, int j ) const {return Get(i,j);}
  __forceinline Coord &operator () ( int i, int j ) {return Set(i,j);}

  bool IsFinite() const;

  // simple access to generic transformation matrix
  __forceinline const Vector3P &Direction() const {return _dir;}
  __forceinline const Vector3P &DirectionUp() const {return _up;}
  __forceinline const Vector3P &DirectionAside() const {return _aside;}
  void SetDirection( const Vector3P &v ) {_dir=v;}
  void SetDirectionUp( const Vector3P &v ) {_up=v;}
  void SetDirectionAside( const Vector3P &v ) {_aside=v;}

  __forceinline float Distance2(const Matrix3P &mat) const
  {
    return _dir.Distance2(mat._dir)+_up.Distance2(mat._up)+_aside.Distance2(mat._aside);
  }

  void Orthogonalize();

    // math for PCA:

  /// Transformation matrix is non-mandatory, if NULL, no transform is performed
  void SetCovarianceCenter ( const AutoArray<Vector3P> &data, const Matrix4P *m =NULL );

  bool SetEigenStandard ( Matrix3PPar cov, Coord *eig =NULL );

  bool IsOrthogonal ( Vector3P *sqrSize ) const;
  /// calculate matrix determinant
  float Determinant() const;
  /// calculate determinant of upper-left 2x2 submatrix
  float Determinant2x2() const
  {
    return Get(0,0)*Get(1,1)-Get(1,0)*Get(0,1);
  }

	//ENF compatible versions
	__forceinline Vector3P GetRow(int i) const 
	{
		return i == 0 ? _aside : i == 1 ? _up : _dir;
	}

	__forceinline Matrix3P& RollPitchYaw(float rollDeg, float pitchDeg, float yawDeg)
	{
		SetRollPitchYawScale(*this, rollDeg, pitchDeg, yawDeg, 1.0f);
		return *this;
	}
};

// define EMPTY constructors
TypeIsSimple(Matrix3P);

//-----------------------------------------------------------------------
class Matrix4P
{
  friend class Vector3P;
  
  // homogenous matrix - transformations
  private:
  Matrix3P _orientation;
  Vector3P _position;
  
  __forceinline Coord Get( int i, int j ) const {return _orientation.Get(i,j);}
  __forceinline Coord &Set( int i, int j ) {return _orientation.Set(i,j);}

  __forceinline Coord GetPos( int i ) const	{return _position.Get(i);}
  __forceinline Coord &SetPos( int i ) {return _position.Set(i);}

  public:

	//!Temporary for compatibility with Enforce
	const Vector3P& operator[](int n) const
	{
		if(n == 0)
			return _orientation.Direction();
		else if(n == 1)
			return _orientation.DirectionUp();
		else if(n == 2)
			return _orientation.DirectionAside();
		else if(n == 3)
			return _position;
		Assert(!"Bad index");
		return VZeroP;
	}
  // functions that load matrix with data
  // used internaly in constuctors, but may be useful also to other purpose
  void SetIdentity();
  bool IsIdentity() const;
  void SetZero();
  void SetTranslation( Vector3PPar offset );
  void SetRotationX( Coord angle );
  void SetRotationY( Coord angle );
  void SetRotationZ( Coord angle );
  void SetRotationAxis(Vector3PPar axis, Coord angle) {_orientation.SetRotationAxis(axis,angle);_position=VZeroP;}
  //! Sets the matrix to express rotation according to specified sine and cosine of the angle
  void SetRotationY(Coord s, Coord c);
  
  void SetScale( Coord x, Coord y, Coord z );
  //void SetPerspective( Coord cNear, Coord cFar, Coord cLeft, Coord cTop );
  void SetPerspective( Coord cLeft, Coord cTop );

  // sets only 3x3 submatrix
  __forceinline void SetDirectionAndUp( Vector3PPar dir, Vector3PPar up )
  {
    _orientation.SetDirectionAndUp(dir,up);
  }
  __forceinline void SetUpAndAside( Vector3PPar up, Vector3PPar aside )
  {
    _orientation.SetUpAndAside(up,aside);
  }
  __forceinline void SetUpAndDirection( Vector3PPar up, Vector3PPar dir )
  {
    _orientation.SetUpAndDirection(up,dir);
  }
  __forceinline void SetDirectionAndAside( Vector3PPar dir, Vector3PPar aside )
  {
    _orientation.SetDirectionAndAside(dir,aside);
  }

  /// get average scale  (some vectors can be after transformation longer)
  __forceinline void SetScale( float scale ){_orientation.SetScale(scale);}
  __forceinline float Scale() const {return _orientation.Scale();}
  /// get maximal scale
  __forceinline float MaxScale() const {return _orientation.MaxScale();}
  // optimalization calculate scale and maxscale at once  
  void ScaleAndMaxScale(float& scale, float& maxScale) const {_orientation.ScaleAndMaxScale(scale, maxScale);};  
  
  //__forceinline float InvScale() const {return _orientation.InvScale();}

  void SetOriented( Vector3PPar dir, Vector3PPar up ); // sets whole 4x4 matrix
  void SetView( Vector3PPar point, Vector3PPar dir, Vector3PPar up );

  __forceinline  Vector3P InvTransformCoord (Vector3PPar o ) const
  {
    Vector3P aside = _orientation._aside;
    Vector3P up = _orientation._up;
    Vector3P dir = _orientation._dir;

    Matrix3P trans(aside._e[0], up._e[0], dir._e[0],
                  aside._e[1], up._e[1], dir._e[1],
                  aside._e[2], up._e[2], dir._e[2]);
    return trans.TransformVector(o - Position());
  }

  Matrix4P InvTransform(const Matrix4P& b ) const
  {
    // result cannot be one of the sources
    Assert(&b!=this);

    Matrix4P out;

    float x, y, z;
    x=b.Get(0,0);
    y=b.Get(0,1);
    z=b.Get(0,2);
    out.Set(0,0)=(Get(0,0)*x+Get(0,1)*y+Get(0,2)*z);
    out.Set(0,1)=(Get(1,0)*x+Get(1,1)*y+Get(1,2)*z);
    out.Set(0,2)=(Get(2,0)*x+Get(2,1)*y+Get(2,2)*z);
    x=b.Get(1,0);
    y=b.Get(1,1);
    z=b.Get(1,2);
    out.Set(1,0)=(Get(0,0)*x+Get(0,1)*y+Get(0,2)*z);
    out.Set(1,1)=(Get(1,0)*x+Get(1,1)*y+Get(1,2)*z);
    out.Set(1,2)=(Get(2,0)*x+Get(2,1)*y+Get(2,2)*z);
    x=b.Get(2,0);
    y=b.Get(2,1);
    z=b.Get(2,2);
    out.Set(2,0)=(Get(0,0)*x+Get(0,1)*y+Get(0,2)*z);
    out.Set(2,1)=(Get(1,0)*x+Get(1,1)*y+Get(1,2)*z);
    out.Set(2,2)=(Get(2,0)*x+Get(2,1)*y+Get(2,2)*z);
    x=b.GetPos(0) - GetPos(0);
    y=b.GetPos(1) - GetPos(1);
    z=b.GetPos(2) - GetPos(2);
    out.SetPos(0)=(Get(0,0)*x+Get(0,1)*y+Get(0,2)*z);
    out.SetPos(1)=(Get(1,0)*x+Get(1,1)*y+Get(1,2)*z);
    out.SetPos(2)=(Get(2,0)*x+Get(2,1)*y+Get(2,2)*z);
    
    return out;
  }

  void SetMultiply( const Matrix4P &a, const Matrix4P &b )
  {
    // result cannot be one of the sources
    Assert(&a!=this);
    Assert(&b!=this);

    float x, y, z;
    x=b.Get(0,0);
    y=b.Get(1,0);
    z=b.Get(2,0);
    Set(0,0)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,0)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,0)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);
    x=b.Get(0,1);
    y=b.Get(1,1);
    z=b.Get(2,1);
    Set(0,1)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,1)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,1)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);
    x=b.Get(0,2);
    y=b.Get(1,2);
    z=b.Get(2,2);
    Set(0,2)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z);
    Set(1,2)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z);
    Set(2,2)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z);
    x=b.GetPos(0);
    y=b.GetPos(1);
    z=b.GetPos(2);
    SetPos(0)=(a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z+a.GetPos(0));
    SetPos(1)=(a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z+a.GetPos(1));
    SetPos(2)=(a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z+a.GetPos(2));
  }

  void SetAdd( const Matrix4P &a, const Matrix4P &b );
  void SetMultiply( const Matrix4P &a, float b );
  void AddMultiply( const Matrix4P &a, float b );
  __forceinline void InlineSetMultiply( const Matrix4P &a, float op )
  {
    SetMultiply(a,op);
  }
  __forceinline void InlineAddMultiply( const Matrix4P &a, float op )
  {
    AddMultiply(a,op);
  }
  __forceinline void InlineSetAdd( const Matrix4P &a, const Matrix4P &b )
  {
    SetAdd(a,b);
  }

  void SetMultiplyByPerspective( const Matrix4P &A, const Matrix4P &B );
  void SetInvertRotation( const Matrix4P &op );
  void SetInvertScaled( const Matrix4P &op );
  void SetInvertGeneral( const Matrix4P &op );
  //void SetNormalTransform( const Matrix4P &op ); // in fact result is Matrix3P
  
  // placeholder parameter describes constructor type
  Matrix4P
  (
    float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23
  )
  {
    Set(0,0)=m00,Set(0,1)=m01,Set(0,2)=m02;
    Set(1,0)=m10,Set(1,1)=m11,Set(1,2)=m12;
    Set(2,0)=m20,Set(2,1)=m21,Set(2,2)=m22;
    SetPos(0)=m03,SetPos(1)=m13,SetPos(2)=m23;
  }
  __forceinline Matrix4P(){}
  __forceinline Matrix4P(bool ident)
	{
    if(ident) 
      this->SetIdentity();
	}
  __forceinline Matrix4P(Matrix3P mat, Vector3P origin):_orientation(mat), _position(origin){}
  __forceinline Matrix4P(Vector3P row0, Vector3P row1, Vector3P row2, Vector3P origin):_orientation(row0, row1, row2), _position(origin){}
  //__forceinline Matrix4P( enum _mIdentity ){SetIdentity();}
  __forceinline Matrix4P( enum _noInit ){}
  __forceinline Matrix4P( enum _guardInit ):_orientation(GuardInit),_position(GuardInit){}
  //__forceinline Matrix4P( enum _mZero ){SetZero();}
  __forceinline Matrix4P( enum _mTranslation, Vector3PPar offset ){SetTranslation(offset);}
  __forceinline Matrix4P( enum _mRotationX, Coord angle ){SetRotationX(angle);}
  __forceinline Matrix4P( enum _mRotationY, Coord angle ){SetRotationY(angle);}
  __forceinline Matrix4P( enum _mRotationZ, Coord angle ){SetRotationZ(angle);}
  __forceinline Matrix4P( enum _mRotationAxis, Vector3PPar axis, Coord angle ) {SetRotationAxis(axis,angle);}
  __forceinline Matrix4P( enum _mScale, Coord x, Coord y, Coord z ){SetScale(x,y,z);}
  __forceinline Matrix4P( enum _mScale, Coord x ){SetScale(x,x,x);}
  // Scale space according to center
  __forceinline Matrix4P( enum _mScale, Coord x, Vector3PPar center ) 
  {
    SetScale(x,x,x);
    SetPosition(center - Rotate(center));
  }
  __forceinline Matrix4P( enum _mPerspective, Coord cLeft, Coord cTop )
  {
    SetPerspective(cLeft,cTop);
  }
  __forceinline Matrix4P( enum _mDirection, Vector3PPar dir, Vector3PPar up )
  {
    SetOriented(dir,up);
  }
  __forceinline Matrix4P( enum _mUpAndDirection, Vector3PPar up, Vector3PPar dir )
  {
    SetUpAndDirection(up,dir);
    SetPosition(VZeroP);
  }
  __forceinline Matrix4P( enum _mView, Vector3PPar point, Vector3PPar dir, Vector3PPar up )
  {
    SetView(point,dir,up);
  }
  __forceinline Matrix4P( enum _mMultiply, const Matrix4P &a, const Matrix4P &b ){SetMultiply(a,b);}
  __forceinline Matrix4P( enum _mMultiply, const Matrix4P &a, float b ){SetMultiply(a,b);}
  __forceinline Matrix4P( enum _mInverseRotation, const Matrix4P &a ){SetInvertRotation(a);}
  __forceinline Matrix4P( enum _mInverseScaled, const Matrix4P &a ){SetInvertScaled(a);}
  __forceinline Matrix4P( enum _mInverseGeneral, const Matrix4P &a ){SetInvertGeneral(a);}
  //Matrix4P( enum _mNormalTransform, const Matrix4P &a ){SetNormalTransform(a);}

  // following operators are defined so that no copy constuctor is used
  // if they are expanded inline, copy is not needed
  __forceinline Matrix4P operator * ( const Matrix4P &op ) const
  {
    // matrix multiplication
    return Matrix4P(MMultiply,*this,op);
  }
  __forceinline Matrix4P operator * ( float op ) const
  {
    return Matrix4P(MMultiply,*this,op);
  }
  Matrix4P operator + ( const Matrix4P &op ) const;
  Matrix4P operator - ( const Matrix4P &op ) const;
  void operator += ( const Matrix4P &op );
  void operator -= ( const Matrix4P &op );
  bool operator == (const Matrix4P &op) const {return _orientation==op._orientation && _position==op._position;}
  bool operator != (const Matrix4P &op) const {return _orientation!=op._orientation || _position!=op._position;}

  __forceinline Vector3P Rotate( Vector3PPar op ) const {return Vector3P(VRotate,*this,op);}
  __forceinline Vector3P FastTransform( Vector3PPar op ) const {return Vector3P(VFastTransform,*this,op);}
  __forceinline Vector3P FastTransformA( Vector3PPar op ) const {return Vector3P(VFastTransformA,*this,op);}
  __forceinline Vector3P operator *( Vector3PPar op ) const {return Vector3P(VMultiply,*this,op);}
  __forceinline Matrix4P InverseRotation() const {return Matrix4P(MInverseRotation,*this);}
  __forceinline Matrix4P InverseScaled() const {return Matrix4P(MInverseScaled,*this);}
  __forceinline Matrix4P InverseGeneral() const {return Matrix4P(MInverseGeneral,*this);}
  //Matrix4P NormalTransform() const {return Matrix4P(MNormalTransform,*this);}
  // member access operators
  //const Coord *operator [] ( int i ) const {return _e[i];}
  //Coord *operator [] ( int i ) {return _e[i];}

  __forceinline Coord operator () ( int i, int j ) const {return Get(i,j);}
  __forceinline Coord &operator () ( int i, int j ) {return Set(i,j);}

  float Characteristic() const; // used in fast comparison
  bool IsFinite() const;

  // simple access to generic transformation matrix
  __forceinline const Matrix3P &Orientation() const {return _orientation;}
  void SetOrientation( const Matrix3P &m ) {_orientation=m;}
  void SetOrientationScaleOnly(float scale) {_orientation.SetScale(scale,scale,scale);}

  __forceinline const Vector3P &Position() const {return _position;}
  __forceinline const Vector3P &Direction() const {return _orientation._dir;}
  __forceinline const Vector3P &DirectionUp() const {return _orientation._up;}
  __forceinline const Vector3P &DirectionAside() const {return _orientation._aside;}
  void SetPosition( const Vector3P &v ) {_position=v;}
  void SetDirection( const Vector3P &v ) {_orientation._dir=v;}
  void SetDirectionUp( const Vector3P &v ) {_orientation._up=v;}
  void SetDirectionAside( const Vector3P &v ) {_orientation._aside=v;}

  __forceinline float Distance2(const Matrix4P &mat) const
  {
    return
    (
      _orientation.Distance2(mat._orientation)+
      _position.Distance2(mat._position)
    );
  }
  void Orthogonalize();

	//ENF compatible functions
	__forceinline Matrix4P(const Matrix4P& mat):_orientation(mat._orientation), _position(mat._position)
	{}

	__forceinline Vector3P TransformCoord(Vector3PPar coord) const 
	{
		return FastTransform(coord);
	}

	__forceinline Vector3P TransformVector(Vector3PPar vec) const 
	{
		return _orientation.TransformVector(vec);
	}

	__forceinline Vector3P InvTransformVector(Vector3PPar vec) const 
	{
		return _orientation.InvTransformVector(vec);
	}

	__forceinline const Vector3P& GetTranslation() const 
	{
		return Position();
	}

	__forceinline const Matrix3P& GetOrientation() const
	{
		return _orientation;
	}

	__forceinline Matrix4P Normalize() const
	{
		Matrix4P res = *this;
		res._orientation.NormalTransform();
		return res;
	}

	__forceinline Matrix4P GetInverse() const
	{
		return InverseGeneral();
	}

	Matrix4P& Reflect(const enf::MPlane& plane);
};

TypeIsSimple(Matrix4P);

// in fact vector is not the same as point
// some operation meaningfull on point have no mean on vector and vice versa
// unmeaningfull are:

// Point+Point
// Point DotProduct Point
// Point CrossProduct Point
// Translate vector
// and maybe some more

// it would be too complicated to build all arithmetics twice so for now
// we suppose the types are equivalent

//-----------------------------------------------------------------------
inline Vector3P sign( Vector3PPar v )
{
  return Vector3P(fSign(v[0]),fSign(v[1]),fSign(v[2]));
}

Vector3P VectorMin( Vector3PPar a, Vector3PPar b );
Vector3P VectorMax( Vector3PPar a, Vector3PPar b );

#define Limit(speed,min,max ) saturateFast(speed,min,max)

void CheckMinMax( Vector3P &min, Vector3P &max, Vector3PPar val );

//-----------------------------------------------------------------------
__forceinline void CheckMinMaxInline( Vector3P &min, Vector3P &max, Vector3PPar val )
{
  saturateMin(min[0],val[0]),saturateMax(max[0],val[0]);
  saturateMin(min[1],val[1]),saturateMax(max[1],val[1]);
  saturateMin(min[2],val[2]),saturateMax(max[2],val[2]);
}

void SaturateMin( Vector3P &min, Vector3PPar val );
void SaturateMax( Vector3P &min, Vector3PPar val );

// it can improve semantics if we strictly distinguish these two types in source
//-----------------------------------------------------------------------
__forceinline Matrix3P Vector3P::Tilda() const 
{
	return Matrix3P(MTilda,*this);
}

//-----------------------------------------------------------------------
__forceinline void Vector3P::SetRotate( const Matrix4P &a, Vector3PPar v )
{
  SetMultiply(a.Orientation(),v);
}

//-----------------------------------------------------------------------
inline float Vector3P::SetPerspectiveProject( const Matrix4P &a, Vector3PPar o )
{
  // optimize: suppose that matrix is scaled and shifted perspective projection, i.e.
  // member [3,2] is 1.0
  // zero members:
  // [0,1], [0,3],
  // [1,0], [1,3], [2,0], [2,1], [2,2]
  // [3,0], [3,1], [3,3]

  //note: [0,0] and [1,1] need not be 1,
  Coord oow=coord(1)/o.Get(2);
  Set(0) = a.Get(0,2)+a.Get(0,0)*o.Get(0)*oow;
  Set(1) = a.Get(1,2)+a.Get(1,1)*o.Get(1)*oow;
  Set(2) = a.Get(2,2)+a.GetPos(2)*oow;
  return oow;
}

//-----------------------------------------------------------------------
//aka TransformCoord
__forceinline void Vector3P::SetMultiply( const Matrix4P &a, Vector3PPar o )
{
  float x = o[0], y = o[1], z = o[2];
  Set(0)=a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z+a.GetPos(0);
  Set(1)=a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z+a.GetPos(1);
  Set(2)=a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z+a.GetPos(2);
}

//-----------------------------------------------------------------------
//aka TransformVector
__forceinline void Vector3P::SetMultiply( const Matrix3P &a, Vector3PPar o )
{ // vector rotation - only 3x3 matrix is used, translation is ignored
  // u=M*v
  float x = o[0], y = o[1], z = o[2];
  Set(0)=a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z;
  Set(1)=a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z;
  Set(2)=a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z;
}

//-----------------------------------------------------------------------
inline void Vector3P::SetFastTransform( const Matrix4P &a, Vector3PPar o )
{
  float x = o[0], y = o[1], z = o[2];
  Set(0)=a.Get(0,0)*x+a.Get(0,1)*y+a.Get(0,2)*z+a.GetPos(0);
  Set(1)=a.Get(1,0)*x+a.Get(1,1)*y+a.Get(1,2)*z+a.GetPos(1);
  Set(2)=a.Get(2,0)*x+a.Get(2,1)*y+a.Get(2,2)*z+a.GetPos(2);
}

//-----------------------------------------------------------------------
/*!
fast normalization of vector, using InvSqrtFast function, not so accurate, but fast,
doesn't use any testing for vector correctness
 */
inline Vector3P Vector3P::NormalizedFast() const
{
	float s = SquareSizeInline();
	return *this*InvSqrtFast(s);
}


#endif // _T_MATH


#endif

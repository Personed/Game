//-----------------------------------------------------------------------------
// File: enf_mathlib.h
//
// Desc: Mathematics library
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_MATHLIB_H
#define ENF_MATHLIB_H

#include <float.h>
#include <math.h>
#include "El/Math/math3d.hpp"
#include "common/enf_types.h"

#ifdef ENF_XENON
	#include <xnamath.h>
#endif

namespace enf
{
/** @defgroup Math3D 3D Mathematic library
@ingroup Common
@{*/

#ifdef ENF_XENON
	#define ENF_VMX_MATH
#endif

//-----------------------------------------------------------------------------
// Vector math
//-----------------------------------------------------------------------------

#if !defined(ENF_POWERPC)
	extern ENF_ALIGN16(uint) fast_sqrt_table[];  // declare table of square roots
#endif

const ENF_ALIGN16(uint) g_imask[4] = {0xffffffff, 0xffffffff, 0xffffffff, 0};

#ifdef __GCC__
	#define BadFloat(a) (!isfinite(a) || isnan(a))
#else
	#define BadFloat(a) (!_finite(a) || _isnan(a))
#endif

#define BadVec(a) (BadFloat(a[0]) || BadFloat(a[1]) || BadFloat(a[2]))
#define BadVec4(a) (BadFloat(a[0]) || BadFloat(a[1]) || BadFloat(a[2]) || BadFloat(a[3]))
#define BadMatrix43(a) (BadFloat(a(0,0)) || BadFloat(a(0,1)) || BadFloat(a(0,2)) || \
								BadFloat(a(1,0)) || BadFloat(a(1,1)) || BadFloat(a(1,2)) || \
								BadFloat(a(2,0)) || BadFloat(a(2,1)) || BadFloat(a(2,2)) || \
								BadFloat(a(0,3)) || BadFloat(a(1,3)) || BadFloat(a(2,3)))
class Vector2;
//class Vector3;
class Vector4;
//class Matrix33;
//class Matrix43;
//class Matrix34;
struct QKey;

const uint PITCH			= 0;
const uint YAW				= 1;
const uint ROLL			= 2;

const uint PLANE_X		= 0;
const uint PLANE_Y		= 4;
const uint PLANE_Z		= 8;
const uint PLANE_ANYX	= 12;
const uint PLANE_ANYY	= 16;
const uint PLANE_ANYZ	= 20;

const float ENF_PI			= 3.14159265358979323846f;					///< PI
const float ENF_PI_HALF		= (3.14159265358979323846f * 0.5f);		///< 0.5*PI
const float ENF_PI2			= 6.28318530717958647692f;					///< 2.0*PI
const float ENF_PI_THIRD	= 1.04719755119659774615f;					///< PI/3
const float ENF_1DEGREE		= 0.01745329251994329576f;
const float ENF_INVPI		= 1.0f/ENF_PI;									///< 1/PI
const float ENF_INVSQRTPI	= (float)(1.0/sqrt(ENF_PI));							///< 1/sqrt(PI)
const float ENF_INVSQRTPI2	= (float)(1.0/sqrt(ENF_PI2));						///< 1/sqrt(PI*2)

#ifdef ENF_POWERPC
	#define FSIGN2INDEX(a)	(__fsel(a, 0.0f, 1.0f))							///< return sign of the int of float (1 = negative, 0 = positive)
	#define ISNEGATIVE(a)	(a < 0.0f)											///< return nonzero number (0x800000000) if float or integer is negative
#else
	#define FSIGN2INDEX(a)	(*(unsigned int *)&a >> 31)					///< return sign of the int of float (1 = negative, 0 = positive)
	#define ISNEGATIVE(a)	(*(unsigned int *)&a & 0x80000000)			///< return nonzero number (0x800000000) if float or integer is negative
#endif

#define ISIGN2INDEX(a)	((unsigned int)a >> 31)					///< return sign of the int of float (1 = negative, 0 = positive)

//#define FP_BITS(fp) (*(unsigned int *)&(fp))

ENF_INLINEFUNC float RAD2DEG(float rad) { return (rad * 180.0f) / ENF_PI; }						///< convert radians to degrees
ENF_INLINEFUNC float DEG2RAD(float deg) { return (deg * ENF_PI) / 180.0f; }						///< convert degrees to radians

const float	DIST_EPSILON = 0.03125f;

static const float ENGINE_UNIT = 32.0f;
ENF_INLINEFUNC float METER2UNIT(float meter) { return meter * ENGINE_UNIT; }
ENF_INLINEFUNC float UNIT2METER(float unit) { return unit * 0.03125f; }

enum ProjType
{
	PT_NONE,
	PT_PERSPECTIVE,
	PT_ORTHOGRAPHIC
};


//!if defined, assert is used for function correctness
//#define MATH_DEBUG_TEST


//!testing functions, slow but exact
#ifdef MATH_DEBUG_TEST

	//-----------------------------------------------------------------------------
	/*!
	round test 
	\param round
	*/
	//-----------------------------------------------------------------------------
	ENF_INLINEFUNC float enf_testround(float f)
	{
		float tr = (int) f;

		if (f >= 0.0f)
		{

			float res = f - tr;
			return res < 0.5f ? tr : tr + 1.0f;
		}

		float res = f - tr;
		return res <= -0.5f ? tr - 1.0f : tr; 
	}


	//-----------------------------------------------------------------------------
	/*!
	floor test 
	\param round
	*/
	//-----------------------------------------------------------------------------
	ENF_INLINEFUNC float enf_testfloor(float f)
	{
		float tr = (int) f;
		return f >= 0.0f ? tr: tr - f > 0.0f ? tr - 1.0f : tr;
	}

	//-----------------------------------------------------------------------------
	/*!
	floor test 
	\param round
	*/
	//-----------------------------------------------------------------------------
	ENF_INLINEFUNC float enf_testtrunc(float f)
	{
		float fl = floorf(f);
		return f >= 0.0f ? fl : fl - f == 0.0 ? fl : fl + 1.0f;
	}

#endif

//-----------------------------------------------------------------------------
/*!
truncate the number -> cut the fractional part of the number
timing:	(int) float			 ~385ms (does inside expensive FPU round state change)
			SSE intrinsic		 ~113ms
			int conversion		 ~124ms
			FPU directly		 ~126ms (24bit prec mode, without FPU state changing)
\param f
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC int enf_truncint(float f)
{
	int res;

	#if defined(ENF_POWERPC)
		//typical conversion -> do it better in XBOX!
		res = (int) f;
	#elif defined (__APPLE__)
		//some problem with SSE code here
		res = (int) f;
	#elif defined (ENF_SSE)
		res = _mm_cvttss_si32(_mm_set_ss(f));
	#else

		int FltInt = *(int *)&f; 

		int mantissa = (FltInt & 0x07fffff) | 0x800000; 
		int exponent = ((FltInt >> 23) & 0xff) - 0x7f; 

		int d = 23-exponent;

		if (d < 0) 
		{
			d = -d;
			res = mantissa << d; 
		}
		else 
			res = mantissa >> d; 

		//possible for very small numbers or very large numbers
		if (d > 31)	res = 0;

		//sign
		if (FltInt & 0x80000000)
			res = -res;

	#endif

	#ifdef MATH_DEBUG_TEST
		Assert(res == (int) enf_testtrunc(f));
	#endif

	return res;
}


//-----------------------------------------------------------------------------
/*!
truncate the float number -> cut the fractional part of the number
timing:	(float) int					124.31
			(float) enf_truncint		105
			floorf						274.61 (doesn't do the same as trunc for negative numbers)
			enf_trunc					74.326
\param f
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_trunc(float f)
{
	float res;

	#ifdef ENF_POWERPC
		float f1 = floorf(f);
		res =  __fself(f, f1, f1 + __fself(f - f1, 0.0f, -1.0f));	
	#elif defined (ENF_SSE)
		res = (float) enf_truncint(f);
	#else
		uint val	= (uint&) f;

		//compute exponent
		uint e		= ((val << 1) >> 24) - 127;

		//how much bits of mantisa to clear
		uint del		= 23 - e;

		//if exponent is negative, the result must be 0 so make a mask
		uint TEST	= (e >> 31) + 0xFFFFFFFF;

		//shift, but this can be more expensive on some system (esp. XBOX) because we are shifting by variable amount 
		val			= ((val >> del) << del) & TEST;
		res			= (float&) val;
	#endif

	#ifdef MATH_DEBUG_TEST
		Assert(res == enf_testtrunc(f));
	#endif	

	return res;
}



//-----------------------------------------------------------------------------
/*!
return both trunc, can be faster if needed both representations
\param	f
\param	fint
\param	ffloat
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC void enf_trunc(float f, int& fint, float &ffloat)
{
	#if defined(ENF_POWERPC)
		float val= ffloat = enf_trunc(f);
		fint		= (int) val;
	#else
		uint val = fint = enf_truncint(f);
		ffloat	= (float) val;
	#endif
}



//--------------------------------------------------------------
/*
convert float to int by rounding (depends on FPU setting), the 
behavior for 0.5 = 0 
timing:	53bit double convert		35		
			sse (this)					111
			testround					450
\param fl	float number
\return		return converted number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC int enf_roundint(float fl)
{
	int res;

	#if defined (ENF_SSE)

		#ifdef FPU_PREC_53BIT
			//this is the fastest method for rounding, but FPU must have 53bit precision, so use define
			//this method works correctly, but for frac part = 0.5 the rounding works for different number up or down
			double val = (double) fl + 68719476736.0*65536.0*1.5;
			res = ((int*)&val)[0];
		#else
			res =  _mm_cvtss_si32(_mm_set_ss(fl));
		#endif		

	#else
		const float ROUND_FLOAT = 0.49999997f;
		res = (int) (fl + (enf_signfloat(fl))*ROUND_FLOAT);
	#endif

	#ifdef MATH_DEBUG_TEST
		Assert(fabs((float) res - fl) <= 1.0f);
	#endif	

	return res;
}



//--------------------------------------------------------------
/*
round float
\param fl	float number
\return		return converted number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC float enf_round(float fl)
{
	return (float) enf_roundint(fl);
}




//--------------------------------------------------------------
/*
ceil of float number -> bigger or equal integer of a number 
\param x	float number
TODO: find something faster
*/
//--------------------------------------------------------------
ENF_INLINEFUNC float enf_ceil(float x)
{
	return ceilf(x);
}


//--------------------------------------------------------------
/*
ceil of float number -> bigger or equal integer of a number 
\param x	float number
TODO: find something faster
*/
//--------------------------------------------------------------
ENF_INLINEFUNC int enf_ceilint(float x)
{
	return (int) ceilf(x);
}



//--------------------------------------------------------------
/*
floor of float number -> less or equal integer of a number 
timing:	floorf		307
			enf_floor	97
\param x	float number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC float enf_floor(float x)
{
	#ifdef ENF_POWERPC
		//res = floorf(x);

		//I found this on apple pages, try it
		
//		double c = __fsel( x, -6755399441055744.0, 6755399441055744.0 );
		double c = __fsel( x, -4503599627370496.0, 4503599627370496.0 );
		double result = (x - c) + c;
		result -= __fsel( x - result, 0.0, 1.0 );
		return result;
		
	#else
		float res;

		uint val		= (uint&) x;
		uint orig	= val;

		//compute exponent
		uint e		= ((val << 1) >> 24) - 127;

		//how much bits of mantissa to clear
		uint del		= 23 - e;

		//if exponent is negative, the result must be 0 so make a mask -> - = 0; + = 0xFFFFFFFF
		uint zero	= (e >> 31) + 0xFFFFFFFF;

		//neg = 0xFFFFFFF if val is negative, else 0
		int neg		= ((int) val >> 31);

		//shift, but this can be more expensive on some system (esp. XBOX) because we are shifting by variable amount 
		val			= ((val >> del) << del) & zero;

		//make diff, if both values are the same (e.g. -1.0f) and are negative, do not add -1
		//diff = 0 if val and orig are the same, else 0xFFFFFFFF
		int diff		= (int) val - (int) orig;
		diff			= ((-diff) | diff) >> 31;

		uint iadd	= 0xBF800000 & neg & diff;

		//sum
		res = (float&) val + (float&) iadd;
#ifdef MATH_DEBUG_TEST
		Assert(res == enf_testfloor(x));
#endif	

		return res;
	#endif
}

//--------------------------------------------------------------
/*
floor of float number -> less or equal integer of a number 
\param x	float number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC int enf_floorint(float x)
{
	return enf_roundint(enf_floor(x));
}

//--------------------------------------------------------------
/*
fastest method to convert from float to int (Vlad Kaipetsky)
\param x	float number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC int enf_toint(float x)
{
	return enf_roundint(x);

	//fastest method to convert from float to int (Vlad Kaipetsky), 
	//but depends on FPU setting, too
/*	const float snap = 3<<22;

	float fval	= x + snap;
	uint res		= (uint&) x;
	res			= (res & 0x007fffff ) - 0x00400000;

	#ifdef MATH_DEBUG_TEST
		Assert(fabs((float) res - x) <= 0.5f);
	#endif	

	return res;
*/	
}



//--------------------------------------------------------------
/*
Make square of number
\param n		number
\return		n*n
*/
//--------------------------------------------------------------
ENF_INLINEFUNC double sqr(double n)
{
	return n * n;
}

ENF_INLINEFUNC float sqr(float n)
{
	return n * n;
}


ENF_INLINEFUNC uint sqr(uint n)
{
	return n * n;
}

ENF_INLINEFUNC uint sqr(int n)
{
	return (uint) (n * n);
}


//--------------------------------------------------------------
/*
random numbers basics
*/
//--------------------------------------------------------------
extern uint _enf_holdrand;

ENF_INLINEFUNC void enf_srand(uint seed)
{
	_enf_holdrand = seed;
}


ENF_INLINEFUNC int enf_rand()
{
	return( ((_enf_holdrand = _enf_holdrand * 214013L + 2531011L) >> 16) & 0x7fff );
}


/*!
to be able to hold some instance data
*/
ENF_INLINEFUNC int enf_randInst(uint& randSeed )
{
	return( ((randSeed = randSeed * 214013L + 2531011L) >> 16) & 0x7fff );
}


//-----------------------------------------------------------------------------
/*!
return an estimation of 1/sqrt(f) -> 12-14 bits of accuracy
timing:	1/sqrt()						32.203
			sse_rcps(sse_sqrt)		6.4218
			int hack	(newton it)		10.108
			invsqrte						4.4817
\param f	
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_invsqrte(float f)
{
//	Assert(f > 1e-10 );

	#if defined(ENF_POWERPC)
		return (f == 0) ? 0.0f : __frsqrte(f);
	#elif defined (ENF_SSE)
		float f1;
		_mm_store_ss(&f1, _mm_rsqrt_ss(_mm_set_ss(f)));
		return f1;
	#else
		float xhalf = 0.5f*f;
		union
		{
			float x;
			int i;
		} u;
		u.x = f;
		u.i = 0x5f3759df - (u.i >> 1);
		f = u.x * (1.5f - xhalf * u.x * u.x);
		return f;
	#endif
}


//-----------------------------------------------------------------------------
/*!
return 1/sqrt(f), normal accuracy
\param f	
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_invsqrt(float f)
{
//	Assert(f > 1e-10 );

	#ifdef ENF_POWERPC
		return 1.0f/__fsqrts(f);
	#else
		return 1.0f/sqrt(f);
	#endif
}

//-----------------------------------------------------------------------------
/*!
return an estimation of sqrt(f), 12-14bits of accuracy
timing:	sqrt			 25.776
			table sqrt	 8.4903	(not so accurate)
			sse sqrt		 10.991	(more accurate -> used)
\param f	
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_sqrte(float f)
{
//	Assert(f >= 0.0f);

	#if defined(ENF_POWERPC)
		//for XMMVECTOR it is better to use __vrsqrtefp()
		return __fsqrts(f);
	#elif defined (ENF_SSE)
		float f1;
		_mm_store_ss(&f1, _mm_sqrt_ss(_mm_set_ss(f)));
		return f1;
	#else
		if (FP_BITS(f) == 0)
			return 0.0;

		FP_BITS(f) = fast_sqrt_table[(FP_BITS(f) >> 8) & 0xFFFF] | ((((FP_BITS(f) - 0x3F800000) >> 1) + 0x3F800000) & 0x7F800000);

		return f;
	#endif
}



//-----------------------------------------------------------------------------
/*!
return an estimation of 1/f, the sse version uses the estimation which is three 
times faster than 1/f but has 14 bits precision
timing:	1/f			 13.921
			sse_rcps		 4.3403
\param f	
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_inve(float f)
{
//	Assert(fabs(f) > 1e-10);

	#if defined(ENF_POWERPC) && !defined(__SNC__)
		return __fres(f);
	#elif defined (ENF_SSE)

		float f1;
		_mm_store_ss(&f1, _mm_rcp_ss(_mm_set_ss(f)));
		return f1;	

	#else
		return 1/f;
	#endif
}


//-----------------------------------------------------------------------------
/*!
Get 2D line equation coefficients k, q of type y = kx + q, usable 
e.g. in shaders when need to blend between distances -> 
dist01 = saturate(dist*k + q)

No release checking is done, so y1 and y0 MUST not be the same

\param y1	value where y = 1
\param y0	value where y = 0
\param K		K coefficient
\param Q		K coefficient
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC void GetLineEq(float y1, float y0, float &K, float &Q)
{
	Assert (fabs(y1 - y0) > 0.00000001f);

	K = 1.0f/(y1 - y0);
	Q = -y0*K;
}



//-----------------------------------------------------------------------------
/*!
Get 2D line equation 

No release checking is done, so y1 and y0 MUST not be the same

\param x		x 
\param y1	x value where y = 1
\param y0	x value where y = 0
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float LineEquation(float x, float y1, float y0)
{
	float K, Q;
	GetLineEq(y1, y0, K, Q);

	return enf_limit(x*K + Q, 0.0f, 1.0f);
}



//-----------------------------------------------------------------------------
/*!
linear interpolation 
\param v1	value for t == 0
\param v2	value for t == 1
\param t weight between v1 and v2
*/
//-----------------------------------------------------------------------------
template<class T> ENF_INLINEFUNC T enf_lerp(const T &v1, const T &v2, float t) 
{
	return v1 + (v2 - v1) * t;
}


//-----------------------------------------------------------------------------
/*!
smooth linear interpolation 
\param v1	value for t == 0
\param v2	value for t == 1
\param t weight between v1 and v2
*/
//-----------------------------------------------------------------------------
template<class T> ENF_INLINEFUNC T enf_smoothlerp(const T &v1, const T &v2, float t) 
{
	float t2 = t*t;

	return v1 + (v2 - v1) * (3*t2 - 2*t*t2);
}


//-----------------------------------------------------------------------------
/*!
saw function -> at x = saw the y = 1 and than is linearly moving to y = 0 at x = 1
and x = 0
\param x		x value from <0..1>
\param saw	value where the result is 1
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC float enf_saw(float x, float saw)
{
	if (x < saw)
	{
		return x/saw;
	}

	if (saw == 1.0f)
	{
		return 0.0f;
	}

	return (1.0f - x)/(1.0f - saw);
}



//-----------------------------------------------------------------------------
/*!
wrap number to specified interval
\param a		number
\param min	minimum
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<class T> ENF_INLINEFUNC T enf_wrap(T a, T min, T max)
{
	Assert2(false, "Don't know now how to wrap this type");
	return (T)0;
}


//-----------------------------------------------------------------------------
/*!
wrap number to specified interval <0..max)
\param a		number
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<class T> ENF_INLINEFUNC T enf_wrap(T a, T max)
{
	Assert2(false, "Don't know now how to wrap this type");
	return (T)0;
}


//-----------------------------------------------------------------------------
/*!
specialization of universal template to float, wrap number to specified interval
\param a		number
\param min	minimum
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<> ENF_INLINEFUNC float enf_wrap<float>(float a, float min, float max) 
{
	float delta = max - min;

	Assert(delta != 0.0f);

	float d = (a - min)/delta;
	float res = d - enf_trunc(d);

	return min + (FSIGN2INDEX(d) + res)*delta;
}


//-----------------------------------------------------------------------------
/*!
specialization of universal template to float, wrap number in <0..max)
\param a		number
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<> ENF_INLINEFUNC float enf_wrap<float>(float a, float max) 
{
	Assert(max != 0.0f);

	float d = a/max;
	float res = d - enf_trunc(d);

	return (FSIGN2INDEX(d) + res)*max;
}




//-----------------------------------------------------------------------------
/*!
specialization of universal template to int, wrap number to specified interval, the 
result is from interval <min, max - 1>
\param a		number
\param min	minimum 
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<> ENF_INLINEFUNC int enf_wrap<int>(int a, int min, int max)
{
	int delta = max - min;
	int res	= (a - min)%delta;

	return min + ISIGN2INDEX(res)*delta + res;
}


//-----------------------------------------------------------------------------
/*!
specialization of universal template to int, wrap number to specified interval, the 
result is from interval <0, max - 1>
\param a		number
\param max	maximum
*/
//-----------------------------------------------------------------------------
template<> ENF_INLINEFUNC int enf_wrap<int>(int a, int max)
{
	int res	= a%max;

	return ISIGN2INDEX(res)*max + res;
}




//--------------------------------------------------------------
/*
Align the number
\param num		number to align
\param align	where to align
\return			aligned number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC uint enf_align(uint num, uint align)
{
	return ((num + (align - 1)) / align) * align;
}

//--------------------------------------------------------------
/*
Align the number to 16 bytes
\param num		number to align
\return			aligned number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC uint enf_align16(uint num)
{
	return ((num + 15) & 0xfffffff0);
}

//--------------------------------------------------------------
/*
Align the number to 8 bytes
\param num		number to align
\return			aligned number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC uint enf_align8(uint num)
{
	return ((num + 7) & 0xfffffff8);
}


//--------------------------------------------------------------
/*
Align the number to 128 bytes
\param num		number to align
\return			aligned number
*/
//--------------------------------------------------------------
ENF_INLINEFUNC uint enf_align128(uint num)
{
	return ((num + 127) & 0xffffff80);
}



//--------------------------------------------------------------
/*!
Convert string to vector
\param str
\return vector
*/
//--------------------------------------------------------------
Vector3 strtovec(const char *str);


//--------------------------------------------------------------
/*!
This function is called by floatToHalf(float i) and should not be called
\param i		float in integer to be converted
*/
//--------------------------------------------------------------
ENF_INLINEFUNC ushort floatToHalfI(uint i)
{
	register int s =  (i >> 16) & 0x00008000;
	register int e = ((i >> 23) & 0x000000ff) - (127 - 15);
	register int m =   i        & 0x007fffff;

	if (e <= 0)
	{
		if (e < -10)
		{
			return 0;
		}
	m = (m | 0x00800000) >> (1 - e);

	return s | (m >> 13);
	}
	else if (e == 0xff - (127 - 15))
	{
		if (m == 0) // Inf
		{
			return s | 0x7c00;
		} 
		else    // NAN
		{
			m >>= 13;
			return s | 0x7c00 | m | (m == 0);
		}
	}
	else
	{
		if (e > 30) // Overflow
		{
			return s | 0x7c00;
		}

		return s | (e << 10) | (m >> 13);
	}
}

//--------------------------------------------------------------
/*!
Convert float to half float
\param i 
\return ushort interpreted as half.. suitable to fill in half f textures
*/
//--------------------------------------------------------------
ENF_INLINEFUNC ushort FloatToHalf(float i)
{
	#if defined(ENF_PS3)
		uint8_t *tmp = (uint8_t*)&i;
		uint32_t bits = ((uint32_t)tmp[0] << 24) | ((uint32_t)tmp[1] << 16) | ((uint32_t)tmp[2] << 8) |(uint32_t)tmp[3];

		if (bits == 0) 
			return 0;

		int32_t e = ((bits & 0x7f800000) >> 23) - 127 + 15;
		if (e < 0) {
			return 0;
		}
		else if (e > 31) {
			e = 31;
		}
		uint32_t s = bits & 0x80000000;
		uint32_t m = bits & 0x007fffff;

		return ((s >> 16) & 0x8000) | ((e << 10) & 0x7c00) | ((m >> 13) & 0x03ff);
	#elif defined(ENF_XENON)
		return XMConvertFloatToHalf(i); 
	#else
		return floatToHalfI(*(uint*)&i);
	#endif
}



//--------------------------------------------------------------
/*!
Gauss value for x, when deviation is 1 and mean is 0, than it's normalized 
Gauss function (better to use the specialized function) :

															-(x - mean)^2
															-------------
									1.0                2*std_dev^2
f(x,mean,std_dev) = -------------------- * e^
							sqrt(2*pi*std_dev^2)


\param x				input to function
\param deviation	influences the shape of curve -> bigger value means more fat 
						and lower curve
\param mean			the center of curve, value with the highest f(x), the curve 
						is mirrored around x = mean
*/
//--------------------------------------------------------------
ENF_INLINEFUNC float GaussFunction(float x, float deviation, float mean = 0.0f)
{
	//optimized, thus not readable ;-)
	float devpow2  = 2.0f*sqr(deviation);
	float invdev   = 1.0f/devpow2;
	float expfunc	= expf(-sqr(x - mean)*invdev);
	
	float invsqrtdev  = 1.0f/sqrt(devpow2);
	return ENF_INVSQRTPI*invsqrtdev*expfunc;
}



//--------------------------------------------------------------
/*!
Inversion of Gauss function
						                                           1
f(y,mean,std_dev) = deviation * sqrt( 2 * ln ( ------------------------------- ))
                                                  y * sqrt (2*PI*deviation)

\param y				y value
\param deviation	influences the shape of curve -> bigger value means more fat 
						and lower curve
*/
//--------------------------------------------------------------
ENF_INLINE float GaussInvFunction(float y, float deviation)
{
	float inv_ydev = 1.0f/(y*deviation);
	float lnfunc   = logf (inv_ydev*ENF_INVSQRTPI2);

	return deviation*sqrt(2.0f*lnfunc);	
}



//--------------------------------------------------------------
/*!
Gauss value for x, for deviation is 1 and mean is 0
\param x				input to function
*/
//--------------------------------------------------------------
ENF_INLINEFUNC float GaussFunctionNorm(float x)
{
	const float invsqrt2pi = 1.0f/sqrt(2.0f*ENF_PI); 
	float expfunc = expf(-0.5f*sqr(x));

	return invsqrt2pi*expfunc;
}



//--------------------------------------------------------------
/*!
get gauss sigma value, to have the function going through x, y
\param x
\param y
*/
//--------------------------------------------------------------
/*ENF_INLINEFUNC float GaussFunctionSigma(float x, float y)
{
	logf (y*invsqrt)


	return x* ;
}
*/


//--------------------------------------------------------------
/*!
return angle in radians of a direction in the same manner as in
mathematic definition of sin and cos -> 0 = [1, 0], ENF_PI_HALF = [0, 1],
ENF_PI = [-1, 0], END_PI + ENF_PI_HALF = [0, -1]
\param dirx	x direction
\param diry	y direction
*/
//--------------------------------------------------------------
ENF_INLINE float enf_dir2angle(float dirx, float diry)
{
	Assert(!(diry == 0.0f && dirx == 0.0f));

	//returns in interval -PI..PI 
	float a = atan2f(diry, dirx);

	if (a < 0.0f)
	{
		a = ENF_PI2 + a;
	}

	return a;
}



////-----------------------------------------------------------------------------
////! Vector3
////-----------------------------------------------------------------------------
//class Vector3
//{
//public:
//
//	//!vector data
//	union
//	{
//		float v[3];				///< array of three floats
//		
//		struct
//		{
//			float x, y, z;		///< x, y, z of the vector
//		};
//	};
//
//
//	//!empty constructor. This *DON'T* initialize the vector!!
//	Vector3()
//	{
//#ifdef _DEBUG
//		//let the uninitialized variables to eat the shit
//		*((int*)&x) = 0x7fc00000;
//		*((int*)&y) = 0x7fc00000;
//		*((int*)&z) = 0x7fc00000;
//#endif
//	}
//
//	/*!
//	Constructor, from three numbers
//	\param ix	x
//	\param iy	y
//	\param iz	z
//	*/
//	ENF_INLINE Vector3(float ix, float iy, float iz): x(ix), y(iy), z(iz) {}
//
//	/*!
//	Constructor, 'f' is copied to all components	
//	\param f		f,f,f
//	*/
//	ENF_INLINE Vector3(float f): x(f), y(f), z(f) {}
//
//	ENF_INLINE Vector3(const Vector3& v): x(v.x), y(v.y), z(v.z) {}
//	Vector3(const Vector2& v);
//
//	/*!
//	constructor, from a pointer to float[3]
//	\param ptr	pointer to floats
//	*/
//	ENF_INLINE Vector3(const float *ptr): x(ptr[0]), y(ptr[1]), z(ptr[2]) {}
//
//	ENF_INLINE Vector3(const Vector4& vec);
//
//	ENF_INLINE Vector3& operator=(const Vector4& vec);
//
//	/*!
//	Gets component by index
//	\return component
//	*/
//	ENF_INLINE float operator[](uint index) const
//	{
//		Assert(index < 3);
//		return v[index];
//	}
//
//	/*!
//	Are vectors equal?
//	\return true if they are equal
//	*/
//	ENF_INLINE bool operator==(const Vector3& in) const
//	{
//		return (x == in.x && y == in.y && z == in.z);
//	}
//
//
//	/*!
//	Are not vectors equal?
//	\return true if they are not equal
//	*/
//	ENF_INLINE bool operator!=(const Vector3& in) const
//	{
//		return (x != in.x || y != in.y || z != in.z);
//	}
//
//
//	/*!
//	Returns lesser components from both operands
//	\return vector 
//	*/
//	ENF_INLINE Vector3 operator<(const Vector3& in) const
//	{
//		return Vector3(enf_min(x, in.x), enf_min(y, in.y), enf_min(z, in.z));
//	}
//
//
//	/*!
//	Returns bigger components from both operands
//	\return vector 
//	*/
//	ENF_INLINE Vector3 operator>(const Vector3& in) const
//	{
//		return Vector3(enf_max(x, in.x), enf_max(y, in.y), enf_max(z, in.z));
//	}
//
//
//	/*!
//	Copy from reference
//	\return reference
//	*/
//	ENF_INLINE Vector3& operator=(const Vector3& in)
//	{
//		x = in.x; y = in.y; z = in.z;
//		return *this;
//	}
//
//	/*!
//	Copy from pointer to floats
//	\return reference
//	*/
//	ENF_INLINE Vector3& operator=(const float *ptr)
//	{
//		x = ptr[0]; y = ptr[1]; z = ptr[2];
//		return *this;
//	}
//
//	/*!
//	Returns scaled vector
//	\param fac		scale factor
//	\return			Vector3(x * fac, y * fac, z * fac)
//	*/
//	ENF_INLINE Vector3 operator*(float fac) const
//	{
//		return Vector3(x * fac, y * fac, z * fac);
//	}
//
//	ENF_INLINE Vector3 operator*(const Vector3& v) const
//	{
//		return Vector3(x * v.x, y * v.y, z * v.z);
//	}
//
//	ENF_INLINE Vector3 Multiply(const Vector3& in) const
//	{
//		return Vector3(x * in.x, y * in.y, z * in.z);
//	}
//
//
//	/*!
//	Scale vector
//	\param fac		scale factor
//	\return			reference
//	*/
//	ENF_INLINE Vector3& operator*=(float fac)
//	{
//		x *= fac; y *= fac; z *= fac;
//		return *this;
//	}
//
//	/*!
//	Add two vectors
//	\param in		vector to add to this one
//	\return			new vector
//	*/
//	ENF_INLINE Vector3 operator+(const Vector3& in) const
//	{
//		return Vector3(x + in.x, y + in.y, z + in.z);
//	}
//
//	/*!
//	Add vector and set
//	\param in		vector to add to this one
//	\return			reference
//	*/
//	ENF_INLINE Vector3& operator+=(const Vector3& in)
//	{
//		x += in.x; y += in.y; z += in.z;
//		return *this;
//	}
//
//	/*!
//	Subtract two vectors
//	\param in		vector to subtract from this one
//	\return			new vector
//	*/
//	ENF_INLINE Vector3 operator-(const Vector3 in) const
//	{
//		return Vector3(x - in.x, y - in.y, z - in.z);
//	}
//
//	/*!
//	Subtract two vectors and set
//	\param in		vector to subtract from this one
//	\return			reference
//	*/
//	ENF_INLINE Vector3& operator-=(const Vector3& in)
//	{
//		x -= in.x; y -= in.y; z -= in.z;
//		return *this;
//	}
//
//	/*!
//	Returns inverse of vector
//	\return		new inverted vector
//	*/
//	ENF_INLINE Vector3 operator-() const
//	{
//		return Vector3(-x, -y, -z);
//	}
//
//
//	/*!
//	Returns cross product of two vectors
//	\param in	second vector
//	\return		new vector of cross product
//	*/
//	ENF_INLINE Vector3 operator^(const Vector3& in) const
//	{
//		return Vector3(y * in.z - z * in.y, z * in.x - x * in.z, x * in.y - y * in.x);
//	}
//
//	/*!
//	Is vector zero
//	\return true if vector has all components zeroed
//	*/
//	ENF_INLINE bool IsZero() const
//	{
//		return (x == 0 && y == 0 && z == 0);
//	}
//
//
//	/*!
//	Is vector epsilon zero
//	\param eps	epsilon for testing
//	\return true if vector has all components within epsilon
//	*/
//	ENF_INLINE bool IsZero(float eps) const
//	{
//		return (fabs(x) < eps && fabs(y) < eps && fabs(z) < eps);
//	}
//
//
//	/*!
//	Zero vector
//	\return reference
//	*/
//	ENF_INLINE Vector3& Zero()
//	{
//		x = y = z = 0.0f;
//		return *this;
//	}
//
//	/*!
//	Linear interpolation between two vectors, lerp == 0 -> return this
//	\param vec		second vector3 for interpolation as reference
//	\param lerp		parameter
//	\return interpolated vector
//	*/
//	ENF_INLINE Vector3 Lerp(const Vector3& vec, float lerp) const
//	{
//		float lx = x, ly = y, lz = z;
//		return Vector3((vec.x - lx) * lerp + lx, (vec.y - ly) * lerp + ly, (vec.z - lz) * lerp + lz);
//	}
//
//	/*!
//	Multiply and add
//	\param add		vector to add
//	\param mult		vector to multiply
//	\return			new vector, add*mult + this
//	*/
//	ENF_INLINE Vector3 MA(const Vector3& add, float mult) const
//	{
//		if(mult == 0)
//			return *this;
//
//		if(mult == 1.0f)
//			return *this + add;
//
//		return Vector3(add.x * mult + x, add.y * mult + y, add.z * mult + z);
//	}
//
//	/*!
//	Dot product
//	\param in	vector for dot product
//	\return		dot product
//	*/
//	ENF_INLINE float Dot(const Vector3& in) const
//	{
//		return x * in.x + y * in.y + z * in.z;
//	}
//
//	/*!
//	Dot product in double for higher precision
//	\param in	vector for dot product
//	\return		dot product
//	*/
//	ENF_INLINE double DotD(const Vector3& in) const
//	{
//		return x * in.x + y * in.y + z * in.z;
//	}
//
//	/*!
//	Euclidean length of vector
//	\return		euclidean length
//	*/
//	ENF_INLINE float Length() const
//	{
//#ifdef ENF_VMX_MATH
//		return XMVector3Length(XMLoadFloat3((const XMFLOAT3 *)v)).x;
//#else
//		double d = x * x + y * y + z * z;
//
//		if(d == 0.0)
//			return d;
//
//		return (float)sqrt(d);
//#endif
//	}
//
//
//	/*!
//	L1(Manhattan) length of vector (sum)
//	\return	manhattan length
//	*/
//	ENF_INLINE float LengthL1() const
//	{
//		return fabs(x) + fabs(y) + fabs(z);
//	}
//
//	/*!
//	Fast euclidean length of vector (fastsqrt lookup function used)
//	\return		fast euclidean length
//	*/
//	ENF_INLINE float QLength() const
//	{
//#ifdef ENF_VMX_MATH
//		return __fsqrts(SquaredLength());
//#else
//		return enf_sqrte(sqr(x) + sqr(y) + sqr(z));
//#endif
//	}
//
//	/*!
//	Euclidean square length of vector
//	\return		square euclidean length
//	*/
//	ENF_INLINE float SquaredLength() const
//	{
//		return (sqr(x) + sqr(y) + sqr(z));
//	}
//
//	/*!
//	Distance from another point
//	\param dest second point
//	\return		euclidean distance between points
//	*/
//	ENF_INLINE float Distance(const Vector3& dest) const
//	{
//#ifdef ENF_VMX_MATH
//		return XMVector3Length(XMVectorSubtract(XMLoadFloat3((const XMFLOAT3 *)v), XMLoadFloat3((const XMFLOAT3 *)dest.v))).x;
//#else
//		return sqrtf(sqr(x - dest.x) + sqr(y - dest.y) + sqr(z - dest.z));
//#endif
//	}
//
//	/*!
//	Planar distance from another point
//	\param dest Second point
//	\return     2D euclidean distance between points.
//	*/
//	ENF_INLINE float DistanceXY( const Vector3& dest ) const
//	{
//		double length = sqr( dest.x - x ) + sqr( dest.y - y);
//		if(length > 0)
//			return (float)sqrt(length);
//
//		return 0.0f;
//	}
//
//	ENF_INLINE Vector3 DirectionXY( const Vector3& dest ) const
//	{
//		float dx = dest.x - x;
//		float dy = dest.y - y;
//		double length = sqr( dx ) + sqr( dy );
//		if(length > 0)
//		{
//			double ilength = 1.0 / sqrt(length);
//			dx *= ilength;
//			dy *= ilength;
//		}
//		return Vector3(dx, dy, 0.0f);
//	}
//
//	/*!
//	L1(Manhattan) distance from another point
//	\param dest second point
//	\return		L1 distance between points
//	*/
//	ENF_INLINE float DistanceL1(const Vector3& dest) const
//	{
//		return fabs(x - dest.x) + fabs(y - dest.y) + fabs(z - dest.z);
//	}
//
//	/*!
//	L1(Manhattan) planar distance from another point
//	\param dest second point
//	\return		L1 2D distance between points
//	*/
//	ENF_INLINE float DistanceL1XY(const Vector3& dest) const
//	{
//		return fabs(x - dest.x) + fabs(y - dest.y);
//	}
//
//	/*!
//	Euclidean square distance between two points
//	\param dest second point
//	\return		euclidean distance between points
//	*/
//	ENF_INLINE float SquaredDistance(const Vector3& dest) const
//	{
//		return sqr(x - dest.x) + sqr(y - dest.y) + sqr(z - dest.z);
//	}
//
//	/*!
//	Planar euclidean square distance between two points
//	\param dest second point
//	\return		2D euclidean distance between points
//	*/
//	ENF_INLINE float SquaredDistanceXY(const Vector3& dest) const
//	{
//		return sqr(x - dest.x) + sqr(y - dest.y);
//	}
//
//	/*!
//	Normalize vector
//	\return		length of vector
//	*/
//	ENF_INLINE float Normalize()
//	{
//		float lx = x, ly = y, lz = z;
//		double length = sqr(lx) + sqr(ly) + sqr(lz);
//
//		if(length > 0)
//		{
//			length = sqrt(length);
//
//			double ilength = 1.0 / length;
//			x = lx * ilength;
//			y = ly * ilength;
//			z = lz * ilength;
//		}
//
//		return (float)length;
//	}
//
//	/*!
//	Returns normalized vector
//	\return		Normalized vector
//	*/
//	ENF_INLINE Vector3 Normalized() const
//	{
//		float lx = x, ly = y, lz = z;
//		double length = sqr(lx) + sqr(ly) + sqr(lz);
//
//		if(length > 0)
//		{
//			double ilength = 1.0 / sqrt(length);
//			return Vector3(lx * ilength, ly * ilength, lz * ilength);
//		}
//
//		return *this;
//	}
//
//	/*!
//	Returns normalized vector
//	\return		Normalized vector
//	*/
//	ENF_INLINE Vector3 QNormalized() const
//	{
//		float lx = x, ly = y, lz = z;
//		float length = sqr(lx) + sqr(ly) + sqr(lz);
//		if(length > 0.0f)
//		{
//			float ilength = enf_invsqrte(length);
//			return Vector3(lx * ilength, ly * ilength, lz * ilength);
//		}
//		return *this;
//	}
//
//
//	/*!
//	Normalize vector using FastInvSqrt
//	*/
//	ENF_INLINE void QNormalize()
//	{
//#ifdef ENF_VMX_MATH
//		XMStoreFloat3((XMFLOAT3*)v, XMVector3NormalizeEst(XMLoadFloat3((const XMFLOAT3*)v)));
//#else
//		float lx = x, ly = y, lz = z;
//		float length = sqr(lx) + sqr(ly) + sqr(lz);
//		if(length > 0.0f)
//		{
//			float ilength = enf_invsqrte(length);
//			x = lx * ilength;
//			y = ly * ilength;
//			z = lz * ilength;
//		}
//#endif
//	}
//
//
//	/*!
//	Clamp vector to specified interval
//	\param minv	 minimum
//	\param maxv	 maximum
//	\return self clamped vector
//	*/
//	ENF_INLINE Vector3 Clamp(float minv, float maxv)
//	{
//		x = enf_limit(x, minv, maxv);
//		y = enf_limit(y, minv, maxv);
//		z = enf_limit(z, minv, maxv);
//		return *this;
//	}
//
//
//	/*!
//	Reflects direction of vector by defined plane normal
//	\param plane
//	Normal of plane
//	\param scale
//	Scale of reflected magnitude. 1.0 is 100%
//	\return
//	Reflected direction of vector
//	*/
//	ENF_INLINE Vector3 Reflect(const Vector3& plane, float scale) const
//	{
//		float backoff = Dot(plane) * (1.0f + scale);
//
//		return Vector3(x - plane.x * backoff, y - plane.y * backoff, z - plane.z * backoff);
//	}
//
//	/*!
//	Rotate point around vector
//	\param point		which point
//	\param degrees		the angle of rotation
//	*/
//	Vector3	RotatePointAroundVector(const Vector3& point, float degrees ) const;
//
//
//	/*!
//	Make one perpendicular vector to this one
//	\return	perpendicular vector
//	*/
//	Vector3 Perpendicular() const;
//
//
//	/*!
//	convert from string
//	\param instr	input string
//	*/
//	void FromString(const char* instr)
//	{
//		*this = strtovec(instr);
//	}
//
//};


//--------------------------------------------------------------
//! Vector4
//--------------------------------------------------------------
class Vector4
{
public:

	union
	{
		float v[4];
		
		struct
		{
			float x, y, z, w;
		};
	};

	ENF_INLINE Vector4(float ix, float iy, float iz, float iw): x(ix), y(iy), z(iz), w(iw) {}

	ENF_INLINE Vector4(float f): x(f), y(f), z(f), w(f) {}

	//!Empty constructor. This *DON'T* initialize vector!
	Vector4()
	{
#ifdef _DEBUG
		//let the uninitialized variables to eat the shit
		*((int*)&x) = 0x7fc00000;
		*((int*)&y) = 0x7fc00000;
		*((int*)&z) = 0x7fc00000;
		*((int*)&w) = 0x7fc00000;
#endif
	}

	Vector4(const Vector3& v, float iw): x(v.X()), y(v.Y()), z(v.Z()), w(iw) {}

	ENF_INLINE Vector4(const float *ptr): x(ptr[0]), y(ptr[1]), z(ptr[2]), w(ptr[3]) {}

	ENF_INLINE Vector4(const Vector3& v): x(v.X()), y(v.Y()), z(v.Z()), w(0.0f) {}

	ENF_INLINE float operator[](uint index) const
	{
		Assert(index <= 3);
		return v[index];
	}

	ENF_INLINE Vector4& operator=(const Vector4& in)
	{
		x = in.x; y = in.y; z = in.z; w = in.w;
		return *this;
	}

	ENF_INLINE Vector4& operator=(const Vector3& in)
	{
		x = in.X(); y = in.Y(); z = in.Z(); w = 0.0f;
		return *this;
	}

	ENF_INLINE Vector4& operator=(const float *ptr)
	{
		x = ptr[0]; y = ptr[1]; z = ptr[2]; w = ptr[3];
		return *this;
	}

	ENF_INLINE bool operator==(const Vector4& vec) const
	{
		return (x == vec.x && y == vec.y && z == vec.z && w == vec.w);
	}

	ENF_INLINE Vector4 operator*(float fac) const
	{
		return Vector4(x * fac, y * fac, z * fac, w * fac);
	}

	ENF_INLINE Vector4& operator*=(float fac)
	{
		x *= fac; y *= fac; z *= fac; w *= fac;
		return *this;
	}

	ENF_INLINE Vector4 operator+(const Vector4& in) const
	{
		return Vector4(x + in.x, y + in.y, z + in.z, w + in.w);
	}

	ENF_INLINE Vector4& operator+=(const Vector4& in)
	{
		x += in.x; y += in.y; z += in.z; w += in.w;
		return *this;
	}

	ENF_INLINE Vector4 operator-(const Vector4& in) const
	{
		return Vector4(x - in.x, y - in.y, z - in.z, w - in.w);
	}

	ENF_INLINE Vector4 operator-(const Vector3& in) const
	{
		return Vector4(x - in.X(), y - in.Y(), z - in.Z(), w);
	}

	ENF_INLINE Vector4& operator-=(const Vector4& in)
	{
		x -= in.x; y -= in.y; z -= in.z; w -= in.w;
		return *this;
	}


	ENF_INLINE Vector4 operator*(const Vector4& in) const
	{
		return Vector4(x * in.x,  y * in.y, z * in.z, w * in.w);
	}

	ENF_INLINE Vector4 Multiply(const Vector4& in) const
	{
		return Vector4(x * in.x, y * in.y, z * in.z, w * in.w);
	}

	ENF_INLINE Vector4 operator-() const
	{
		return Vector4(-x, -y, -z, -w);
	}

	ENF_INLINE Vector4 operator^(const Vector3& in) const
	{
		return Vector4(y * in.Z() - z * in.Y(),
							z * in.X() - x * in.Z(),
							x * in.Y() - y * in.X(),
							0.0f);
	}

	ENF_INLINE Vector4& Zero()
	{
		x = y = z = w = 0.0f;
		return *this;
	}

	ENF_INLINE float Dot(const Vector3& point) const
	{
		return (x * point.X() + y * point.Y() + z * point.Z());
	}

	ENF_INLINE float Dot(const Vector4& point) const
	{
		return (x * point.x + y * point.y + z * point.z + w * point.w);
	}

	ENF_INLINE float Dot3(const Vector4& point) const
	{
		return (x * point.x + y * point.y + z * point.z);
	}

	ENF_INLINE float Normalize()
	{
		float length = sqr(x) + sqr(y) + sqr(z) + sqr(w);

		if(length > 0)
		{
			length = sqrtf(length);

			float ilength = 1.0f / length;
			x *= ilength;
			y *= ilength;
			z *= ilength;
			w *= ilength;
		}

		return length;
	}

	Vector4 Normalized() const
	{
		float length = sqr(x) + sqr(y) + sqr(z) + sqr(w);

		if(length > 0)
		{
			float ilength = 1.0f / sqrtf(length);
			return Vector4(x * ilength, y * ilength, z * ilength, w * ilength);
		}
		return *this;
	}

	ENF_INLINE float QLength() const
	{
#ifdef ENF_VMX_MATH
		return XMVector4LengthEst(XMLoadFloat4((const XMFLOAT4 *)v)).x;
#else
		return enf_sqrte(sqr(x) + sqr(y) + sqr(z) + sqr(w));
#endif
	}

	ENF_INLINE float Length() const
	{
#ifdef ENF_VMX_MATH
		return XMVector4Length(XMLoadFloat4((const XMFLOAT4 *)v)).x;
#else
		return sqrtf(sqr(x) + sqr(y) + sqr(z) + sqr(w));
#endif
	}

	ENF_INLINE void Clamp(float minv, float maxv)
	{
		x = enf_limit(x, minv, maxv);
		y = enf_limit(y, minv, maxv);
		z = enf_limit(z, minv, maxv);
		w = enf_limit(w, minv, maxv);
	}

	ENF_INLINE Vector4 Lerp(const Vector4& vec, float lerp) const
	{
		return Vector4(enf_lerp(x, vec.x, lerp), enf_lerp(y, vec.y, lerp), enf_lerp(z, vec.z, lerp), enf_lerp(w, vec.w, lerp));
	}
};


//ENF_INLINE Vector3& Vector3P::operator=(const Vector4& vec)
//{
//	_e[0] = vec.x; _e[1] = vec.y; _e[2] = vec.z;
//	return *this;
//}

//--------------------------------------------------------------
/*!
quaternion -> x,y,z is imaginary part, w is real part
*/
//--------------------------------------------------------------
class Quaternion: public Vector4
{
public:
	Quaternion(bool init = false)
	{
		if(init) Identity();
	}

	Quaternion(float xx, float yy, float zz, float ww): Vector4(xx, yy, zz, ww) {}

	inline Quaternion(const Matrix43& mat);
	Quaternion(const Matrix33& mat)
	{
		*this = mat;
	}

	Quaternion(const Vector4& vec)
	{
		x = vec.x; y = vec.y; z = vec.z; w = vec.w;
	}

	Quaternion& operator=(const Vector4& vec)
	{
		x = vec.x; y = vec.y; z = vec.z; w = vec.w;
		return *this;
	}

	Quaternion& operator=(const float* vec)
	{
		x = vec[0]; y = vec[1]; z = vec[2]; w = vec[3];
		return *this;
	}

	Quaternion& operator=(const Matrix33& mat);

	Quaternion Lerp(const Quaternion& q, float lerp) const;
	Quaternion NLerp(const Quaternion& q, float lerp) const;
	ENF_INLINE Quaternion Lerp(const Quaternion* q, float lerp) const
	{
		Assert(q != NULL);
		return Lerp(*q, lerp);
	}

	ENF_INLINE Quaternion operator*(float fac) const
	{
		return Quaternion(x * fac, y * fac, z * fac, w * fac);
	}

	ENF_INLINE Quaternion& operator*=(float fac)
	{
		x *= fac;
		y *= fac;
		z *= fac;
		w *= fac;

		return *this;
	}

	ENF_INLINE Quaternion operator*( const Quaternion& a ) const
	{
		return Quaternion(
			w * a.x + x * a.w + y * a.z - z * a.y,
			w * a.y + y * a.w + z * a.x - x * a.z,
			w * a.z + z * a.w + x * a.y - y * a.x,
			w * a.w - x * a.x - y * a.y - z * a.z);
	}

	ENF_INLINE Quaternion operator-() const
	{
		return Quaternion(-x, -y, -z, -w);
	}

	Vector3 operator*( const Vector3& a ) const;

	ENF_INLINE float Dot(const Quaternion& quat) const
	{
		return x * quat.x + y * quat.y + z * quat.z + w * quat.w;
	}

	ENF_INLINE bool IsIdentity() const
	{
		return (x == 0.0f && y == 0.0f && z == 0.0f && w == 1.0f);
	}

	ENF_INLINE Quaternion& Identity()
	{
		x = y = z = 0.0f;
		w = 1.0f;
		return *this;
	}

	ENF_INLINE float Length() const
	{
		return sqrtf(sqr(x) + sqr(y) + sqr(z) + sqr(w));
	}

	float ToPitch() const;
	Vector3 ToMatrixRow(uint row) const;

	/*!
	conjugate
	*/
	ENF_INLINE Quaternion Conjugate() const
	{
		return Quaternion(-x, -y, -z, w);
	}

	/*!
	norm of quaternion
	*/
	ENF_INLINE float Norm() const 
	{
		return x*x + y*y + z*z + w*w;
	}

	/*!
	inverse, which is (q*)/|q|
	*/
	ENF_INLINE Quaternion Inverse() const
	{
		Assert(Norm() > 0.0f);
		return Conjugate()*(1.0f/Norm());
	}

	/*!
	exponent function of quaternion
	*/
	Quaternion Exponential() const;

	/*!
	logarithmic function of quaternion
	*/
	Quaternion Logarithm() const;

	/*!
	spherical cubic interpolation, based on bezier cubic 
	\param t		time in <0..1>
	\param P		first quaternion
	\param A		"tangent" quaternion of P
	\param B		"tangent" quaternion of Q
	\param Q		second quaternion
	*/
	static Quaternion Squad(float t, const Quaternion& P, const Quaternion& A, const Quaternion& B, const Quaternion& Q)
	{
		Quaternion lerpPQ = P.Lerp(Q, t);
		Quaternion lerpAB = A.Lerp(B, t);

		return lerpPQ.Lerp(lerpAB, 2.0f*t*(1.0f - t));
	}

};
//
////--------------------------------------------------------------
////! general row-major 4x4 matrix
////--------------------------------------------------------------
class Matrix44
{
private:
	static ENF_ALIGN16(Matrix44) Ident;
public:
	
	static const Matrix44& GetIdent()
	{
		return Ident;
	}

	union
	{
		float	m[4][4];
		struct
		{
			float _11, _12, _13, _14;
			float	_21, _22, _23, _24;
			float	_31, _32, _33, _34;
			float	_41, _42, _43, _44;
		};
	};

	Matrix44& operator=(const Quaternion& quat);

	Matrix44& operator=(const Matrix43& mat);
	Matrix44& operator=(const Matrix43* mat);

	ENF_INLINE Matrix44& operator=(const Matrix44& mat)
	{
		/*
		//strange, this was enabled too

		_11 = mat._11; _12 = mat._12; _13 = mat._13; _14 = mat._14;
		_21 = mat._21; _22 = mat._22; _23 = mat._23; _24 = mat._24;
		_31 = mat._31; _32 = mat._32; _33 = mat._33; _34 = mat._34;
		_41 = mat._41; _42 = mat._42; _43 = mat._43; _44 = mat._44;
		*/

#ifdef ENF_VMX_MATH
		XMStoreFloat4x4((XMFLOAT4X4*)this, XMLoadFloat4x4((const XMFLOAT4X4*)&mat));
#else
		CopyMem(m, mat.m, sizeof(m));
#endif
		return *this;
	}

	Matrix44 (const Matrix44& mat)
	{
		#ifdef ENF_VMX_MATH
			XMStoreFloat4x4((XMFLOAT4X4*)this, XMLoadFloat4x4((const XMFLOAT4X4*)&mat));
		#else
			CopyMem(m, mat.m, sizeof(m));
		#endif
	}

	Matrix44& RollPitchYaw(float roll, float pitch, float yaw);
	Matrix44& Roll(float roll);
	Matrix44& Pitch(float pitch);
	Matrix44& Yaw(float yaw);

	Matrix44 MultiplyTranspose(const Matrix44& mat) const;

	Vector4 TransformPlane(const Vector4& plane) const;
	Vector4 TransformCoord(const Vector4& coord) const;
	Vector3 TransformCoord(const Vector3& coord) const;
	Vector3 TransformVector(const Vector3& in) const
	{
		float x = in.X(), y = in.Y(), z = in.Z();
		return Vector3(x * _11 + y * _21 + z * _31,
							x * _12 + y * _22 + z * _32,
							x * _13 + y * _23 + z * _33);
	}
	Vector4 InvTransformCoord(const Vector4& coord) const;
	Vector4 InvTransformCoord(const Vector3& coord) const;
	Vector4 InvTransformVector(const Vector3& vec) const;
	Vector4 InvTransformVector(const Vector4& vec) const;

	Matrix44 PerspectiveFovLH(float fovy, float aspect, float zn, float zf);
	Matrix44 OrthoLH(float width, float height, float zn, float zf);
	Matrix44 OrthoRH(float width, float height, float zn, float zf);

	//!Sets rotational part to scale factors. Translation part is untouched
	void SetScale(float xscale, float yscale, float zscale)
	{
		_11 = xscale; _12 = 0.0f; _13 = 0.0f; _14 = 0.0f;
		_21 = 0.0f; _22 = yscale; _23 = 0.0f; _24 = 0.0f;
		_31 = 0.0f; _32 = 0.0f; _33 = zscale; _34 = 0.0f;
	}

	Matrix44 GetInverse() const;
	Matrix44 GetTranspose() const;

	//solver for linear equations by LUP decomposition
	void SeparateLU(Matrix44 &L, Matrix44 &U) const;
	bool LUPDecomposition(Matrix44 &LU, uint *P) const;
	bool SolveEquationLUP(const Vector4 &b, Vector4 &x) const;


	Matrix44 operator*(const Matrix44& mat) const;
	Matrix44& operator*=(const Matrix44& mat);
	ENF_INLINE bool operator==(const Matrix44& mat) const
	{
		return (
			_11 == mat._11 && _12 == mat._12 && _13 == mat._13 && _14 == mat._14 && 
			_21 == mat._21 && _22 == mat._22 && _23 == mat._23 && _24 == mat._24 && 
			_31 == mat._31 && _32 == mat._32 && _33 == mat._33 && _34 == mat._34 && 
			_41 == mat._41 && _42 == mat._42 && _43 == mat._43 && _44 == mat._44);
	}

	ENF_INLINE Vector4& operator[](uint row)
	{
		Assert(row <= 3);
		return *((Vector4*)&m[row][0]);
	}

	ENF_INLINE const Vector4& operator[](uint row) const
	{
		Assert(row <= 3);
		return *((Vector4*)&m[row][0]);
	}

	ENF_INLINE const Vector4& GetRow(uint row) const
	{
		Assert(row <= 3);
		return *((Vector4*)&m[row][0]);
	}

	ENF_INLINE Vector4 GetColumn(uint col) const
	{
		Assert(col <= 3);
		return Vector4(m[0][col], m[1][col], m[2][col], m[3][col]);
	}

	void SetRow(uint row, const Vector3& v)
	{
		Assert(row <= 3);
		m[row][0] = v.X();
		m[row][1] = v.Y();
		m[row][2] = v.Z();
		m[row][3] = (row == 3) ? 1.0f : 0.0f;
	}

	void SetRow(uint row, const Vector4& v)
	{
		Assert(row <= 3);
		m[row][0] = v.x;
		m[row][1] = v.y;
		m[row][2] = v.z;
		m[row][3] = v.w;
	}

	Matrix44& Identity()
	{
#ifdef ENF_VMX_MATH
		XMStoreFloat4x4((XMFLOAT4X4*)this, XMMatrixIdentity());
#else
		_11 = 1.0f; _12 = 0.0f; _13 = 0.0f; _14 = 0.0f;
		_21 = 0.0f; _22 = 1.0f; _23 = 0.0f; _24 = 0.0f;
		_31 = 0.0f; _32 = 0.0f; _33 = 1.0f; _34 = 0.0f;
		_41 = 0.0f; _42 = 0.0f; _43 = 0.0f; _44 = 1.0f;
#endif
		return *this;
	}

	Matrix44& Zero()
	{
		_11 = 0.0f; _12 = 0.0f; _13 = 0.0f; _14 = 0.0f;
		_21 = 0.0f; _22 = 0.0f; _23 = 0.0f; _24 = 0.0f;
		_31 = 0.0f; _32 = 0.0f; _33 = 0.0f; _34 = 0.0f;
		_41 = 0.0f; _42 = 0.0f; _43 = 0.0f; _44 = 0.0f;
		return *this;
	}

	bool IsIdentity() const
	{
#ifdef ENF_VMX_MATH
		return XMMatrixIsIdentity(XMLoadFloat4x4((const XMFLOAT4X4*)this)) != 0;
#else
		if(_11 != 1.0f || _12 != 0.0f || _13 != 0.0f || _14 != 0.0f ||
			_21 != 0.0f || _22 != 1.0f || _23 != 0.0f || _24 != 0.0f ||
			_31 != 0.0f || _32 != 0.0f || _33 != 1.0f || _34 != 0.0f ||
			_41 != 0.0f || _42 != 0.0f || _43 != 0.0f || _44 != 1.0f)
			return false;

		return true;
#endif
	}


	Matrix44(bool ident = false)
	{
		if(ident) Identity();
	}

	Matrix44(const Vector4& row0,
				const Vector4& row1, 
				const Vector4& row2, 
				const Vector4& row3)
	{
		_11 = row0.x; _12 = row0.y; _13 = row0.z; _14 = row0.w;
		_21 = row1.x; _22 = row1.y; _23 = row1.z; _24 = row1.w;
		_31 = row2.x; _32 = row2.y; _33 = row2.z; _34 = row2.w;
		_41 = row3.x; _42 = row3.y; _43 = row3.z; _44 = row3.w;
	}

	Matrix44(float f11, float f12, float f13, float f14,
				float f21, float f22, float f23, float f24,
				float f31, float f32, float f33, float f34,
				float f41, float f42, float f43, float f44)
	{
		_11 = f11; _12 = f12; _13 = f13; _14 = f14;
		_21 = f21; _22 = f22; _23 = f23; _24 = f24;
		_31 = f31; _32 = f32; _33 = f33; _34 = f34;
		_41 = f41; _42 = f42; _43 = f43; _44 = f44;
	}

	Matrix44(const Matrix43& mat);

};

////--------------------------------------------------------------
////! general row-major 4x3 matrix
////--------------------------------------------------------------
//class Matrix43
//{
//private:
//
//	static Matrix43 Ident;
//
//	static void _multiply4(Matrix43* out, const Matrix43* ENF_RESTRICT a, const Matrix43* ENF_RESTRICT b);
//	static void _multiply3(Matrix43* out, const Matrix43* ENF_RESTRICT a, const Matrix43* ENF_RESTRICT b);
//
//public:
//	
//	static const Matrix43& GetIdent()
//	{
//		return Ident;
//	}
//
//	union
//	{
//		float	m[4][3];
//		struct
//		{
//			float _11, _12, _13;
//			float	_21, _22, _23;
//			float	_31, _32, _33;
//			float	_41, _42, _43;
//		};
//   };
//
//	Matrix43(const Quaternion& quat)
//	{
//		*this = quat;
//	}
//
//	Matrix43(const Matrix34& mat)
//	{
//		*this = mat;
//	}
//
//
//	Matrix43(const Quaternion& quat, const Vector3& trans);
//
//	Matrix43(const Vector3& origin):
//		_11(1.0f), _12(0.0f), _13(0.0f),
//		_21(0.0f), _22(1.0f), _23(0.0f),
//		_31(0.0f), _32(0.0f), _33(1.0f),
//		_41(origin.X()), _42(origin.Y()), _43(origin.Z())
//	{
//	}
//
//	ENF_INLINE Matrix43(const Matrix33& m);
//	ENF_INLINE Matrix43(const Matrix33& m, const Vector3& origin);
//
//	Matrix43(const Vector3& row0, const Vector3& row1, const Vector3& row2, const Vector3& row3):
//		_11(row0.x), _12(row0.y), _13(row0.z),
//		_21(row1.x), _22(row1.y), _23(row1.z),
//		_31(row2.x), _32(row2.y), _33(row2.z),
//		_41(row3.x), _42(row3.y), _43(row3.z)
//	{
//	}
//
//	Matrix43(float f11, float f12, float f13,
//		float f21, float f22, float f23,
//		float f31, float f32, float f33,
//		float f41, float f42, float f43):
//		_11(f11), _12(f12), _13(f13),
//		_21(f21), _22(f22), _23(f23),
//		_31(f31), _32(f32), _33(f33),
//		_41(f41), _42(f42), _43(f43)
//	{
//	}
//
//	Matrix43(bool ident = false)
//	{
//		if(ident) Identity();
//	}
//
//	Matrix43(const Matrix44& mat):
//		_11(mat._11), _12(mat._12), _13(mat._13),
//		_21(mat._21), _22(mat._22), _23(mat._23),
//		_31(mat._31), _32(mat._32), _33(mat._33),
//		_41(mat._41), _42(mat._42), _43(mat._43)
//	{
//	}
//
//	ENF_INLINE Matrix43& operator=(const Matrix34& mat);
//	Matrix43& operator=(const Matrix44& mat);
//	Matrix43& operator=(const Quaternion& quat);
//	bool operator==(const Matrix43& mat) const;
//	bool operator!=(const Matrix43& mat) const
//	{
//		return !(operator==(mat));
//	}
//
//	//return just orientation part of matrix.
//	ENF_INLINE const Matrix33& GetOrientation() const;
//
//	//sets just orientation part of matrix
//	ENF_INLINE Matrix43& SetOrientation(const Matrix33& orient);
//	ENF_INLINE Matrix43& SetTranslation(const Vector3& trans)
//	{
//		_41 = trans.x; _42 = trans.y; _43 = trans.z;
//		return *this;
//	}
//
//	const Vector3& GetTranslation() const
//	{
//		return *(Vector3*)&_41;
//	}
//
//	Matrix43&	AroundVector( const Vector3 dir, float degrees );
//
//	ENF_INLINE Vector4 TransformPlane(const Vector4& in) const
//	{
//		Vector4 out;
//		out.x = in.x * _11 + in.y * _21 + in.z * _31,
//		out.y = in.x * _12 + in.y * _22 + in.z * _32,
//		out.z = in.x * _13 + in.y * _23 + in.z * _33,
//		out.w = in.w + (out.x * _41 + out.y * _42 + out.z * _43);
//		return out;
//	}
//
//	ENF_INLINE Vector3 TransformCoord(const Vector3& in) const
//	{
//		float x = in.x, y = in.y, z = in.z;
//		return Vector3(x * _11 + y * _21 + z * _31 + _41,
//							x * _12 + y * _22 + z * _32 + _42,
//							x * _13 + y * _23 + z * _33 + _43);
//	}
//
//	ENF_INLINE Vector3 TransformVector(const Vector3& in) const
//	{
//		float x = in.x, y = in.y, z = in.z;
//		return Vector3(x * _11 + y * _21 + z * _31,
//							x * _12 + y * _22 + z * _32,
//							x * _13 + y * _23 + z * _33);
//	}
//
//	ENF_INLINE Vector3 InvTransformCoord(const Vector3& in) const
//	{
//		float x = in.x - _41, y = in.y - _42, z = in.z - _43;
//		return Vector3(x * _11 + y * _12 + z * _13,
//							x * _21 + y * _22 + z * _23,
//							x * _31 + y * _32 + z * _33);
//	}
//
//	ENF_INLINE Vector3 InvTransformVector(const Vector3& in) const
//	{
//		float x = in.x, y = in.y, z = in.z;
//		return Vector3(x * _11 + y * _12 + z * _13,
//							x * _21 + y * _22 + z * _23,
//							x * _31 + y * _32 + z * _33);
//	}
//
//
//	ENF_INLINE Matrix43 InvTransform(const Matrix43& mat) const
//	{
//		return Matrix43(
//			InvTransformVector(mat.GetRow(0)),
//			InvTransformVector(mat.GetRow(1)),
//			InvTransformVector(mat.GetRow(2)),
//			InvTransformCoord(mat.GetRow(3)));
//	}
//
//	//!Sets rotational part to scale factors. Translation part is untouched
//	Matrix43& SetScale(float scaleX, float scaleY, float scaleZ);
//
//	//!Sets rotational part to angles. Translation part is untouched
//	Matrix43& RollPitchYaw(float roll, float pitch, float yaw);
//	
//	//!Sets rotational part to angles&scale. Translation part is untouched
//	Matrix43& RollPitchYawScale(float roll, float pitch, float yaw, float scale);
//	
//	//!Sets rotational part to roll angle. Translation part is untouched
//	Matrix43& Roll(float roll);
//	
//	//!Sets rotational part to pitch angle. Translation part is untouched
//	Matrix43& Pitch(float pitch);
//	
//	//!Sets rotational part to yaw angle. Translation part is untouched
//	Matrix43& Yaw(float yaw);
//
//	Matrix43& Reflect(const Vector4& plane);
//
//	ENF_INLINE float ToRoll() const
//	{
//		return RAD2DEG(atan2(_23,_33));
//	}
//	ENF_INLINE float ToPitch() const
//	{
//		return RAD2DEG(atan2(_12, _11));
//	}
//	ENF_INLINE float ToYaw() const
//	{
//		return -RAD2DEG(asin(enf_limit(_13, -1.0f, 1.0f)));
//	}
//
//	Vector3 ToAngles() const
//	{ 
//		//TODO: resolve singular cases!
//		return Vector3(ToPitch(), ToYaw(), ToRoll());
//	}
//
//	bool IsOrthoNormal() const;
//	bool IsOrthogonal() const;
//	bool IsNormalized() const;
//
//	bool IsRotated() const
//	{
//		if(_11 != 1.0f || _22 != 1.0f || _33 != 1.0f ||
//			_12 != 0.0f || _13 != 0.0f ||
//			_21 != 0.0f || _23 != 0.0f ||
//			_31 != 0.0f || _32 != 0.0f)
//			return true;
//
//		return false;
//	}
//
//	ENF_INLINE bool IsIdentity() const
//	{
//#ifdef ENF_VMX_MATH
//		return XMMatrixIsIdentity(XMLoadFloat4x3((const XMFLOAT4X3*)this)) != 0;
//#else
//		return (_11 == 1.0f && _12 == 0.0f && _13 == 0.0f &&
//			_21 == 0.0f && _22 == 1.0f && _23 == 0.0f &&
//			_31 == 0.0f && _32 == 0.0f && _33 == 1.0f &&
//			_41 == 0.0f && _42 == 0.0f && _43 == 0.0f);
//#endif
//	}
//
//	ENF_INLINE Matrix43& operator=(const Matrix43& mat)
//	{
//		_11 = mat._11; _12 = mat._12; _13 = mat._13;
//		_21 = mat._21; _22 = mat._22; _23 = mat._23;
//		_31 = mat._31; _32 = mat._32; _33 = mat._33;
//		_41 = mat._41; _42 = mat._42; _43 = mat._43;
//		return *this;
//	}
//
//	ENF_INLINE Matrix43& SetMultiply4A16(const Matrix43& m1, const Matrix43& m2)
//	{
//#ifdef ENF_VMX_MATH
//		XMMATRIX mm1 = XMLoadFloat4x3A16((const XMFLOAT4X3A16*)&m1);
//		XMMATRIX mm2 = XMLoadFloat4x3A16((const XMFLOAT4X3A16*)&m2);
//		XMStoreFloat4x3A16((XMFLOAT4X3A16*)this, XMMatrixMultiply(mm2, mm1));
//#else
//		_multiply4(this, &m1, &m2);
//#endif
//		return *this;
//	}
//
//#ifdef ENF_VMX_MATH
//	ENF_INLINE Matrix43& SetMultiply4A16(const Matrix43& m1, const QKey& key);
//#endif
//
//	ENF_INLINE  Matrix43 operator*(const Matrix43& mat) const
//	{
//		Matrix43 result;
//
//		_multiply4(&result, this, &mat);
//		return result;
//	}
//
//	ENF_INLINE  Matrix43& operator*=(const Matrix43& mat)
//	{
//		Matrix43 result;
//
//		_multiply4(&result, this, &mat);
//		*this = result;
//		return *this;
//	}
//
//	ENF_INLINE  const Vector3& operator[](uint row) const 
//	{
//		Assert(row <= 3);
//		return *((Vector3*)&m[row][0]);
//	}
//
//	ENF_INLINE const Vector3& GetRow(uint row) const
//	{
//		Assert(row <= 3);
//		return *((Vector3*)&m[row][0]);
//	}
//
//	ENF_INLINE Vector4 GetColumn(uint col) const
//	{
//		Assert(col <= 2);
//		return Vector4(m[0][col], m[1][col], m[2][col], m[3][col]);
//	}
//
//	ENF_INLINE void SetRow(uint row, const Vector3& vec)
//	{
//		Assert(row <= 3);
//
//		m[row][0] = vec.x;
//		m[row][1] = vec.y;
//		m[row][2] = vec.z;
//	}
//
//	//! Returns determinant of 3x3 sub-matrix
//	float GetDeterminant() const
//	{
//		return _11 * _22 * _33 + _12 * _23 * _31 + _13 * _21 * _32 -
//				_13 * _22 * _31 - _12 * _21 * _33 - _11 * _23 * _32;
//	}
//
//	Matrix43 GetInverse() const;
//
//	Matrix43& Normalize();
//
//	Matrix43& Identity()
//	{
//		_11 = 1.0f; _12 = 0.0f; _13 = 0.0f;
//		_21 = 0.0f; _22 = 1.0f; _23 = 0.0f;
//		_31 = 0.0f; _32 = 0.0f; _33 = 1.0f;
//		_41 = 0.0f; _42 = 0.0f; _43 = 0.0f;
//		return *this;
//	}
//
//	ENF_INLINE Matrix43& Zero()
//	{
//		_11 = 0.0f; _12 = 0.0f; _13 = 0.0f;
//		_21 = 0.0f; _22 = 0.0f; _23 = 0.0f;
//		_31 = 0.0f; _32 = 0.0f; _33 = 0.0f;
//		_41 = 0.0f; _42 = 0.0f; _43 = 0.0f;
//		return *this;
//	}
//};
//
//inline Quaternion::Quaternion(const Matrix43& mat)
//{
//	*this = mat.GetOrientation();
//}
//
//-----------------------------------------------------------------------
//!Column major 3x4 matrix, used for GPU friendly layout
//--------------------------------------------------------------
class Matrix34
{
private:

	static Matrix34 Ident;

	static void _multiply4(Matrix34* out, const Matrix34* ENF_RESTRICT a, const Matrix34* ENF_RESTRICT b)
	{
		enf_assert(out != a);
		enf_assert(out != b);

		float x, y, z;
		x = a->_11; y = a->_21; z = a->_31;
		out->_11 = x * b->_11 + y * b->_12 + z * b->_13;
		out->_21 = x * b->_21 + y * b->_22 + z * b->_23;
		out->_31 = x * b->_31 + y * b->_32 + z * b->_33;
		out->_41 = x * b->_41 + y * b->_42 + z * b->_43 + a->_41;
		x = a->_12; y = a->_22; z = a->_32;
		out->_12 = x * b->_11 + y * b->_12 + z * b->_13;
		out->_22 = x * b->_21 + y * b->_22 + z * b->_23;
		out->_32 = x * b->_31 + y * b->_32 + z * b->_33;
		out->_42 = x * b->_41 + y * b->_42 + z * b->_43 + a->_42;
		x = a->_13; y = a->_23; z = a->_33;
		out->_13 = x * b->_11 + y * b->_12 + z * b->_13;
		out->_23 = x * b->_21 + y * b->_22 + z * b->_23;
		out->_33 = x * b->_31 + y * b->_32 + z * b->_33;
		out->_43 = x * b->_41 + y * b->_42 + z * b->_43 + a->_43;
	}

	template<class T> ENF_INLINE Matrix34& CopyMatrix(const T& mat)
	{
		_11 = mat._11; _21 = mat._21; _31 = mat._31; _41 = mat._41;
		_12 = mat._12; _22 = mat._22; _32 = mat._32; _42 = mat._42;
		_13 = mat._13; _23 = mat._23; _33 = mat._33; _43 = mat._43;
		return *this;
	}


public:
	
	static const Matrix34& GetIdent()
	{
		return Ident;
	}

	union
	{
		float	m[3][4];
		struct
		{
			float _11, _21, _31, _41;
			float	_12, _22, _32, _42;
			float	_13, _23, _33, _43;
		};
   };

	Matrix34(const Vector3& origin):
		_11(1.0f), _21(0.0f), _31(0.0f), _41(origin.x),
		_12(0.0f), _22(1.0f), _32(0.0f), _42(origin.y),
		_13(0.0f), _23(0.0f), _33(1.0f), _43(origin.z)
	{
	}

		Matrix34(const Vector3& row0, const Vector3& row1, const Vector3& row2, const Vector3& row3):
	_11(row0.x), _21(row1.x), _31(row2.x), _41(row3.x),
		_12(row0.y), _22(row1.y), _32(row2.y), _42(row3.y),
		_13(row0.z), _23(row1.z), _33(row2.z), _43(row3.z)
	{
	}
	Matrix34(const Vector4& col0, const Vector4& col1, const Vector4& col2):
		_11(col0.x), _21(col0.y), _31(col0.z), _41(col0.w),
		_12(col1.x), _22(col1.y), _32(col1.z), _42(col1.w),
		_13(col2.x), _23(col2.y), _33(col2.z), _43(col2.w)
	{
	}

	Matrix34(float f11, float f12, float f13,
		float f21, float f22, float f23,
		float f31, float f32, float f33,
		float f41, float f42, float f43):
		_11(f11), _21(f21), _31(f31), _41(f41),
		_12(f12), _22(f22), _32(f32), _42(f42),
		_13(f13), _23(f23), _33(f33), _43(f43)
	{
	}

	Matrix34(bool ident = false)
	{
		if(ident) Identity();
	}

/*	Matrix34(const Matrix43& mat)
	{
		CopyMatrix<Matrix43>(mat);
	}*/
	Matrix34(const Matrix34& mat)
	{
		CopyMatrix<Matrix34>(mat);
	}
	Matrix34(const Matrix44& mat)
	{
		CopyMatrix<Matrix44>(mat);
	}
/*
	ENF_INLINE Matrix34& operator=(const Matrix43& mat)
	{
		return CopyMatrix<Matrix43>(mat);
	}
	*/
	ENF_INLINE Matrix34& operator=(const Matrix34& mat)
	{
		return CopyMatrix<Matrix34>(mat);
	}

	ENF_INLINE Matrix34& operator+=(const Matrix34& mat)
	{
		SetColumn(0, GetColumn(0) + mat.GetColumn(0));
		SetColumn(1, GetColumn(1) + mat.GetColumn(1));
		SetColumn(2, GetColumn(2) + mat.GetColumn(2));
		return *this;
	}

	ENF_INLINE Matrix34& operator=(const Matrix44& mat)
	{
		return CopyMatrix<Matrix44>(mat);
	}

	ENF_INLINE Matrix34& operator=(const Matrix4P& mat)
	{
		//transpose?
		_11 = mat.GetOrientation()._11; _21 = mat.GetOrientation()._21; _31 = mat.GetOrientation()._31; _41 = mat.GetTranslation().x;
		_12 = mat.GetOrientation()._12; _22 = mat.GetOrientation()._22; _32 = mat.GetOrientation()._32; _42 = mat.GetTranslation().y;
		_13 = mat.GetOrientation()._13; _23 = mat.GetOrientation()._23; _33 = mat.GetOrientation()._33; _43 = mat.GetTranslation().z;

		return *this;
	}

	ENF_INLINE Vector3 GetTranslation() const
	{
		return Vector3(_41, _42, _43);
	}

	ENF_INLINE Matrix34& SetTranslation(const Vector3& pos) 
	{
		_41 = pos.x; _42 = pos.y; _43 = pos.z;
		return *this;
	}
	ENF_INLINE Matrix34& SetOrientation(const Matrix33& orient);

	ENF_INLINE Vector3 TransformCoord(const Vector3& in) const
	{
		float x = in.x, y = in.y, z = in.z;
		return Vector3(x * _11 + y * _21 + z * _31 + _41,
							x * _12 + y * _22 + z * _32 + _42,
							x * _13 + y * _23 + z * _33 + _43);
	}

	ENF_INLINE Vector3 TransformVector(const Vector3& in) const
	{
		float x = in.x, y = in.y, z = in.z;
		return Vector3(x * _11 + y * _21 + z * _31,
							x * _12 + y * _22 + z * _32,
							x * _13 + y * _23 + z * _33);
	}

	ENF_INLINE Vector3 InvTransformCoord(const Vector3& in) const
	{
		float x = in.x - _41, y = in.y - _42, z = in.z - _43;
		return Vector3(x * _11 + y * _12 + z * _13,
							x * _21 + y * _22 + z * _23,
							x * _31 + y * _32 + z * _33);
	}

	ENF_INLINE Vector3 InvTransformVector(const Vector3& in) const
	{
		float x = in.x, y = in.y, z = in.z;
		return Vector3(x * _11 + y * _12 + z * _13,
							x * _21 + y * _22 + z * _23,
							x * _31 + y * _32 + z * _33);
	}

	ENF_INLINE bool IsIdentity() const
	{
		return (_11 == 1.0f && _12 == 0.0f && _13 == 0.0f &&
			_21 == 0.0f && _22 == 1.0f && _23 == 0.0f &&
			_31 == 0.0f && _32 == 0.0f && _33 == 1.0f &&
			_41 == 0.0f && _42 == 0.0f && _43 == 0.0f);
	}

	ENF_INLINE  Matrix34 Multiply4(const Matrix34& mat) const
	{
		Matrix34 result;

		_multiply4(&result, this, &mat);
		return result;
	}

	ENF_INLINE  Matrix34 operator*(const Matrix34& mat) const
	{
		Matrix34 result;

		_multiply4(&result, this, &mat);
		return result;
	}

	ENF_INLINE Matrix34 operator*(const Matrix44& mat) const
	{
		Matrix34 out;
		float x, y, z;
		x = mat._11; y = mat._21; z = mat._31;
		out._11 = x * _11 + y * _12 + z * _13;
		out._21 = x * _21 + y * _22 + z * _23;
		out._31 = x * _31 + y * _32 + z * _33;
		out._41 = x * _41 + y * _42 + z * _43 + mat._41;
		x = mat._12; y = mat._22; z = mat._32;
		out._12 = x * _11 + y * _12 + z * _13;
		out._22 = x * _21 + y * _22 + z * _23;
		out._32 = x * _31 + y * _32 + z * _33;
		out._42 = x * _41 + y * _42 + z * _43 + mat._42;
		x = mat._13; y = mat._23; z = mat._33;
		out._13 = x * _11 + y * _12 + z * _13;
		out._23 = x * _21 + y * _22 + z * _23;
		out._33 = x * _31 + y * _32 + z * _33;
		out._43 = x * _41 + y * _42 + z * _43 + mat._43;
		return out;
	}

	ENF_INLINE  Matrix34& operator*=(const Matrix34& mat)
	{
		Matrix34 result;

		_multiply4(&result, this, &mat);
		*this = result;
		return *this;
	}

	ENF_INLINE void SetRow(uint row, const Vector3& v)
	{
		Assert(row <= 3);
		m[0][row] = v.x; m[1][row] = v.y; m[2][row] = v.z;
	}

	ENF_INLINE Vector3 GetRow(uint row) const
	{
		Assert(row <= 3);
		return Vector3(m[0][row], m[1][row], m[2][row]);
	}

	ENF_INLINE const Vector4& GetColumn(uint col) const
	{
		Assert(col <= 2);
		return ((const Vector4*)this)[col];
	}

	ENF_INLINE void SetColumn(uint col, const Vector4& v)
	{
		Assert(col <= 2);
		m[col][0] = v.x; m[col][1] = v.y; m[col][2] = v.z; m[col][3] = v.w;
	}

	ENF_INLINE Vector3 operator[](int row) const 
	{
		return GetRow(row);
	}

	float GetDeterminant() const
	{
		return _11 * _22 * _33 + _12 * _23 * _31 + _13 * _21 * _32 -
				_13 * _22 * _31 - _12 * _21 * _33 - _11 * _23 * _32;
	}
	Matrix34 GetInverse() const;

	Matrix34& Yaw(float yaw)
	{
		enf_assert(false);
		//TBD
		return Identity();
	}

	Matrix34& Roll(float roll)
	{
		enf_assert(false);
		//TBD
		return Identity();
	}

	Matrix34& Pitch(float pitch)
	{
		enf_assert(false);
		//TBD
		return Identity();
	}

	Matrix34& Identity()
	{
		_11 = 1.0f; _21 = 0.0f; _31 = 0.0f; _41 = 0.0f;
		_12 = 0.0f; _22 = 1.0f; _32 = 0.0f; _42 = 0.0f;
		_13 = 0.0f; _23 = 0.0f; _33 = 1.0f; _43 = 0.0f;
		return *this;
	}
	ENF_INLINE Matrix34& Zero()
	{
		_11 = 0.0f; _21 = 0.0f; _31 = 0.0f; _41 = 0.0f;
		_12 = 0.0f; _22 = 0.0f; _32 = 0.0f; _42 = 0.0f;
		_13 = 0.0f; _23 = 0.0f; _33 = 0.0f; _43 = 0.0f;
		return *this;
	}

	Matrix34 Scale(float scale) const
	{
		return Matrix34(GetColumn(0) * scale, GetColumn(1) * scale, GetColumn(2) * scale);
	}
};
/*
ENF_INLINE Matrix43& Matrix43::operator=(const Matrix34& mat)
{
	_11 = mat._11; _21 = mat._21; _31 = mat._31; _41 = mat._41;
	_12 = mat._12; _22 = mat._22; _32 = mat._32; _42 = mat._42;
	_13 = mat._13; _23 = mat._23; _33 = mat._33; _43 = mat._43;
	return *this;
}
*/
//-----------------------------------------------------------------------
//!Rotational row-major matrix
//--------------------------------------------------------------
//class Matrix33
//{
//private:
//
//public:
//	static void _multiply(Matrix33& out, const Matrix33& a, const Matrix33& b);
//
//	union
//	{
//		float	m[3][3];
//		struct
//		{
//			float _11, _12, _13;
//			float	_21, _22, _23;
//			float	_31, _32, _33;
//		};
//	};
//
//	Matrix33(bool ident = false)
//	{
//		if(ident) Identity();
//	}
//
//	Matrix33(const Vector3& row0, const Vector3& row1, const Vector3& row2)
//	{
//		_11 = row0.x; _12 = row0.y; _13 = row0.z;
//		_21 = row1.x; _22 = row1.y; _23 = row1.z;
//		_31 = row2.x; _32 = row2.y; _33 = row2.z;
//	}
//	Matrix33(float f11, float f12, float f13,
//		float f21, float f22, float f23,
//		float f31, float f32, float f33):
//		_11(f11), _21(f21), _31(f31),
//		_12(f12), _22(f22), _32(f32),
//		_13(f13), _23(f23), _33(f33)
//	{
//	}
//
//	Matrix33& RollPitchYaw(const Vector3& angles);
//	Matrix33& RollPitchYaw(float roll, float pitch, float yaw);
//	Matrix33& RollPitchYawScale(float roll, float pitch, float yaw, float scale);
//	Matrix33& Roll(float roll);
//	Matrix33& Pitch(float pitch);
//	Matrix33& Yaw(float yaw);
//	Vector3 GetRow(uint row) const
//	{
//		Assert(row < 3);
//		return Vector3(m[row][0], m[row][1], m[row][2]);
//	}
//
//	//solver for linear equations by LUP decomposition
//	void SeparateLU(Matrix33 &L, Matrix33 &U) const;
//	bool LUPDecomposition(Matrix33 &LU, uint *P) const;
//	bool SolveEquationLUP(const Vector3 &b, Vector3 &x) const;
//
//	bool IsIdentity() const
//	{
//		if(_11 != 1.0f || _12 != 0.0f || _13 != 0.0f ||
//			_21 != 0.0f || _22 != 1.0f || _23 != 0.0f ||
//			_31 != 0.0f || _32 != 0.0f || _33 != 1.0f)
//			return false;
//
//		return true;
//	}
//	
//	bool IsNormalized() const;
//
//	ENF_INLINE  Matrix33& operator=(const Matrix33& mat)
//	{
//		_11 = mat._11; _12 = mat._12; _13 = mat._13;
//		_21 = mat._21; _22 = mat._22; _23 = mat._23;
//		_31 = mat._31; _32 = mat._32; _33 = mat._33;
//		return *this;
//	}
//
//	void SetScale(float scaleX, float scaleY, float scaleZ)
//	{
//		_11 = scaleX; _12 = 0.0f; _13 = 0.0f;
//		_21 = 0.0f; _22 = scaleY; _23 = 0.0f;
//		_31 = 0.0f; _32 = 0.0f; _33 = scaleZ;
//	}
//
//	ENF_INLINE  Matrix33 operator*(const Matrix33& mat) const
//	{
//		Matrix33 result;
//
//		_multiply(result, *this, mat);
//		return result;
//	}
//
//	ENF_INLINE  Matrix33 operator*(float scale) const
//	{
//		return Matrix33(_11 * scale, _12 * scale, _13 * scale,
//							_21 * scale, _22 * scale, _23 * scale,
//							_31 * scale, _32 * scale, _33 * scale);
//	}
//
//	ENF_INLINE  Matrix33& operator*=(const Matrix33& mat)
//	{
//		Matrix33 result;
//
//		_multiply(result, *this, mat);
//		*this = result;
//		return *this;
//	}
//	
//	ENF_INLINE bool operator==(const Matrix33& mat) const
//	{
//		return _11 == mat._11 && _12 == mat._12 && _13 == mat._13 && 
//				_21 == mat._21 && _22 == mat._22 && _23 == mat._23 && 
//				_31 == mat._31 && _32 == mat._32 && _33 == mat._33;
//	}
//
//	ENF_INLINE  const Vector3& operator[](uint row) const 
//	{
//		Assert(row <= 2);
//		return *((Vector3*)&m[row][0]);
//	}
//
//	ENF_INLINE void SetRow(uint row, const Vector3& vec)
//	{
//		Assert(row <= 2);
//
//		m[row][0] = vec.x;
//		m[row][1] = vec.y;
//		m[row][2] = vec.z;
//	}
//
//	Matrix33& Identity()
//	{
//		_11 = 1.0f; _12 = 0.0f; _13 = 0.0f;
//		_21 = 0.0f; _22 = 1.0f; _23 = 0.0f;
//		_31 = 0.0f; _32 = 0.0f; _33 = 1.0f;
//		return *this;
//	}
//
//	ENF_INLINE Vector3 TransformVector(const Vector3& in) const
//	{
//		float x = in.x, y = in.y, z = in.z;
//		return Vector3(x * _11 + y * _21 + z * _31,
//							x * _12 + y * _22 + z * _32,
//							x * _13 + y * _23 + z * _33);
//	}
//
//	ENF_INLINE Vector3 TransformCoord(const Vector3& in) const
//	{
//		return TransformVector(in);
//	}
//
//	ENF_INLINE Vector3 InvTransformVector(const Vector3& in) const
//	{
//		float x = in.x, y = in.y, z = in.z;
//		return Vector3(x * _11 + y * _12 + z * _13,
//							x * _21 + y * _22 + z * _23,
//							x * _31 + y * _32 + z * _33);
//	}
//
//	ENF_INLINE Vector3 InvTransformCoord(const Vector3& in) const
//	{
//		return InvTransformVector(in);
//	}
//
//	ENF_INLINE Matrix33 InvTransform(const Matrix33& mat) const
//	{
//		return Matrix33(
//			InvTransformVector(mat.GetRow(0)),
//			InvTransformVector(mat.GetRow(1)),
//			InvTransformVector(mat.GetRow(2)));
//	}
//};

//-----------------------------------------------------------------------
//!Translation&Rotation key. Used for animations
//-----------------------------------------------------------------------
struct QKey
{
	Quaternion	m_qQuat;
	Vector3		m_vTrans;
	/*
	ENF_INLINE Matrix43 ToMatrix() const
	{
		return Matrix43(m_qQuat, m_vTrans);
	}

	Matrix43 ToParent(const Matrix43& parent) const;
	*/
	QKey Lerp(const QKey& q2, float lerp) const;
	QKey LerpRot(const QKey& q, float lerp) const;
	QKey& Identity()
	{
		m_vTrans.Zero();
		m_qQuat.Identity();
		return *this;
	}
	QKey(const Quaternion& quat, const Vector3& trans): m_qQuat(quat), m_vTrans(trans) {}
	QKey(const Matrix33& mat, const Vector3& trans): m_qQuat(mat), m_vTrans(trans) {}
	QKey(): m_qQuat(0,0,0,1), m_vTrans(0,0,0) {}
};
//
//ENF_INLINE Matrix34& Matrix34::SetOrientation(const Matrix33& orient)
//{
//	_11 = orient._11; _12 = orient._12; _13 = orient._13;
//	_21 = orient._21; _22 = orient._22; _23 = orient._23;
//	_31 = orient._31; _32 = orient._32; _33 = orient._33;
//	return *this;
//}
//ENF_INLINE Matrix43& Matrix43::SetOrientation(const Matrix33& orient)
//{
//	_11 = orient._11; _12 = orient._12; _13 = orient._13;
//	_21 = orient._21; _22 = orient._22; _23 = orient._23;
//	_31 = orient._31; _32 = orient._32; _33 = orient._33;
//	return *this;
//}
//
//ENF_INLINE const Matrix33& Matrix43::GetOrientation() const
//{
//	return *((const Matrix33*)this);
//}
//
//ENF_INLINE Matrix43::Matrix43(const Matrix33& m):
//		_11(m._11), _12(m._12), _13(m._13),
//		_21(m._21), _22(m._22), _23(m._23),
//		_31(m._31), _32(m._32), _33(m._33),
//		_41(0), _42(0), _43(0)
//{
//}
//
//ENF_INLINE Matrix43::Matrix43(const Matrix33& m, const Vector3& origin):
//		_11(m._11), _12(m._12), _13(m._13),
//		_21(m._21), _22(m._22), _23(m._23),
//		_31(m._31), _32(m._32), _33(m._33),
//		_41(origin.x), _42(origin.y), _43(origin.z)
//{
//}
//
//
//#ifdef ENF_VMX_MATH
//ENF_INLINE Matrix43& Matrix43::SetMultiply4A16(const Matrix43& m1, const QKey& key)
//{
//
//	XMMATRIX pmat = XMLoadFloat4x3((const XMFLOAT4X3*)&m1);
//	XMMATRIX qmat = XMMatrixRotationQuaternion(XMLoadFloat4((const XMFLOAT4*)&key.m_qQuat));
//	XMVECTOR trans = XMLoadFloat3((const XMFLOAT3*)&key.m_vTrans);
//	trans.w = 1.0f;
//	qmat.r[3] = trans;
//
//	XMStoreFloat4x3((XMFLOAT4X3*)this, XMMatrixMultiply(qmat, pmat));
//	return *this;
//}
//#endif

//-----------------------------------------------------------------------
//!MPlane, struct defining the plane in 3D
//-----------------------------------------------------------------------
struct MPlane: public Vector4
{
	int		Type;		///< special type of plane, 0 -> general plane


	//!empty constructor
	MPlane(): Type(0)
	{
	}

	/*!
	constructor
	\param normal	normal vector of the plane
	\param dist		distance to the origin
	*/
	MPlane(const Vector3& normal, float dist, int type = 0): Vector4(normal.X(), normal.Y(), normal.Z(), dist), Type(type) {}
	MPlane(const Vector4& plane): Vector4(plane), Type(0) {}

	MPlane(const Vector3& p0, const Vector3& p1, const Vector3& p2)
	{
		FromPoints(p0, p1, p2);
	}

	/*!
	Compute distance from the point
	\param point	point
	\return			distance to the plane
	*/
	ENF_INLINE float Distance(const Vector3& point) const
	{
		return Dot(point) - w;
	}

	/*!
	operator ==, are the planes equal
	\return	true is the planes are equal (the coeficients)
	*/
	bool operator==(const MPlane& mp) const
	{
		return (Type == mp.Type && x == mp.x && y == mp.y && z == mp.z && w == mp.w);
	}

	/*!
	operator =, copy
	\param mp	plane
	*/
	void operator=(const MPlane& mp)
	{
		x = mp.x; y = mp.y; z = mp.z; w = mp.w;
		Type = mp.Type;
	}

	/*!
	operator =, set normal vector of plane
	\param vec	normal vector
	\return		reference to plane
	*/
	MPlane& operator=(const Vector3& vec)
	{
		x = vec.X(); y = vec.Y(); z = vec.Z();
		return *this;
	}

	/*!
	operator =, set normal vector and distance
	\param vec	vector
	\return		reference to plane
	*/
	MPlane& operator=(const Vector4& vec)
	{
		x = vec.x; y = vec.y; z = vec.z; w = vec.w;
		Type = 0;
		return *this;
	}

	
	/*!
	Construct plane from three points
	\param p0	first point
	\param p1	second point
	\param p2	third point
	\return		reference to plane
	*/
	MPlane& FromPoints(const Vector3& p0, const Vector3& p1, const Vector3& p2);


	/*!
	Construct not normalized plane from three points
	\param p0	first point
	\param p1	second point
	\param p2	third point
	\return		reference to plane
	*/
	MPlane& FromPointsNN(const Vector3& p0, const Vector3& p1, const Vector3& p2);
	
	/*!
	Project point to the plane
	\param pt	point
	\return		the projected point
	*/
	Vector3 Project(const Vector3& pt) const;


	/*!
	Make inversion of the plane
	\return		reference to plane
	*/
	MPlane &Invert()
	{
		x = -x; y = -y; z = -z; w = -w;
		return *this;
	}

	ENF_INLINE float DistanceOptimized(const Vector3& point) const
	{
		int n = Type;
		if(n < 0)
			return *(float *)((char *)point._e + (n & 0xff)) - w;
		else
			return Dot(point) - w;
	}

	ENF_INLINE uint SideOptimized(const Vector3& pos) const
	{
		float d = DistanceOptimized(pos);

#ifdef ENF_INTEL
		return FSIGN2INDEX(d);
#else
		return (d < 0) ? 1 : 0;
#endif
	}

	ENF_INLINE uint IntersectLineOptimized(const Vector3& v1, const Vector3& v2, float& d1, float& d2) const
	{
		int n = Type;
		float lw = w;

		if(n < 0)
		{
			n &= 0xff;

			d1 = *(float *)((char *)v1._e + n) - lw;
			d2 = *(float *)((char *)v2._e + n) - lw;
		}
		else
		{
			float lx = x;
			float ly = y;
			float lz = z;
			d1 = (v1.X() * lx + v1.Y() * ly + v1.Z() * lz) - lw;
			d2 = (v2.X() * lx + v2.Y() * ly + v2.Z() * lz) - lw;
		}

		if(d1 >= -DIST_EPSILON && d2 >= -DIST_EPSILON)
			return 1;

		if(d1 < DIST_EPSILON && d2 < DIST_EPSILON)
			return 2;
		
		return 3;
	}
	
	ENF_INLINE uint IntersectLine(const Vector3& v1, const Vector3& v2, float& d1, float& d2) const
	{
		float lw = w;

		float lx = x;
		float ly = y;
		float lz = z;
    d1 = (v1.X() * lx + v1.Y() * ly + v1.Z() * lz) - lw;
		d2 = (v2.X() * lx + v2.Y() * ly + v2.Z() * lz) - lw;
		
		if(d1 >= -DIST_EPSILON && d2 >= -DIST_EPSILON)
			return 1;

		if(d1 < DIST_EPSILON && d2 < DIST_EPSILON)
			return 2;

		return 3;
	}
};

//-----------------------------------------------------------------------
//! Axis aligned bounding volume. Bounding box is defined by minimal and maximal bounds.
//-----------------------------------------------------------------------
class AAB
{
protected:
	union 
	{
		struct 
		{
			Vector3	Mins; ///<Minimal bounds
			Vector3	Maxs; ///<Maximal bounds
		};

		float MinMax[6];
	};

public:
//!Constructor creating AAB set to defined point
	AAB(const Vector3& point): Mins(point), Maxs(point) {}

//!Constructor creating AAB with defined extents
	AAB(const Vector3& mins, const Vector3& maxs): Mins(mins), Maxs(maxs) {}

//!Constructor creating AAB with defined extents
	AAB(float minx, float miny, float minz, float maxx, float maxy, float maxz): Mins(minx, miny, minz), Maxs(maxx, maxy, maxz) {}

/*!
Basic constructor.
\param init When set to true, AAB is initialized with maximums on numeric minimum and vice versa.
*/
	AAB(bool init = false)
	{
		if(init)
			Init();
	}

/*!
Tells whether is bounding volume valid. Valid AAB must
not be negative sized (minimums greater than maximums)
\return True when AAB is valid, false otherwise
*/
	ENF_INLINE bool IsValid() const
	{
		return (Mins.X() <= Maxs.X() &&
				Mins.Y() <= Maxs.Y() &&
				Mins.Z() <= Maxs.Z());
	}

/*!
Tells whether is bounding volume zero sized.
\return True when AAB is zero sized, false otherwise
*/
	ENF_INLINE bool IsZero() const
	{
		return (Mins.IsZero() && Maxs.IsZero());
	}

//!Copy operator.
	ENF_INLINE const AAB& operator=(const AAB& aabb)
	{
		Mins = aabb.Mins;
		Maxs = aabb.Maxs;
		return *this;
	}

	ENF_INLINE bool operator==(const AAB& aabb) const
	{
		return Mins == aabb.Mins && Maxs == aabb.Maxs;
	}

/*!
Gets center of AAB. It's the middle between
maximums and minimums of volume.
\return Point in the center of AAB
*/
	ENF_INLINE Vector3 GetCenter() const
	{
		return (Mins + Maxs) * 0.5f;
	}

/*!
Gets vector from center to corner of AAB.
\return Vector to corner
*/
	ENF_INLINE Vector3 GetCorner() const
	{
		return (Maxs - Mins) * 0.5f;
	}

/*!
Gets minimum of AAB
\return Minimums.
*/
	ENF_INLINE const ::Vector3& GetMins() const
	{
		return Mins;
	}

/*!
Gets maximum of AAB
\return Maximums.
*/
	ENF_INLINE const ::Vector3& GetMaxs() const
	{
		return Maxs;
	}

	/*!
	return minmax array of min and max vectors
	*/
	ENF_INLINE const Vector3* GetMinMax() const
	{
		return (const Vector3*) &MinMax;
	}

/*!
Returns enlarged AAB to nearest multiply of value
in each axis/direction

\param size Size of grid to which we quantize
\return enlarged box
*/
	ENF_INLINE AAB GetEnlargeQuantized(float size) const
	{
		float hsize = size * 0.5f;
		return AAB(floor(Mins.X() / size) * size - hsize,
			floor(Mins.Y() / size) * size - hsize,
			floor(Mins.Z() / size) * size - hsize,
			ceil(Maxs.X() / size) * size + hsize,
			ceil(Maxs.Y() / size) * size + hsize,
			ceil(Maxs.Z() / size) * size + hsize);
	}
	
	ENF_INLINE AAB GetEnlargeQuantized(float xsize, float ysize, float zsize) const
	{
		return AAB(
			floor(Mins.X() / xsize) * xsize - (xsize * 0.5f),
			floor(Mins.Y() / ysize) * ysize - (ysize * 0.5f),
			floor(Mins.Z() / zsize) * zsize - (zsize * 0.5f),
			ceil(Maxs.X() / xsize) * xsize + (xsize * 0.5f),
			ceil(Maxs.Y() / ysize) * ysize + (ysize * 0.5f),
			ceil(Maxs.Z() / zsize) * zsize + (zsize * 0.5f));
	}

/*!
Gets maximum absolute extent in each axis
\return
Maximum extents
*/
	Vector3 GetExtents() const
	{
		return Vector3(enf_max(-Mins.X(), Maxs.X()), enf_max(-Mins.Y(), Maxs.Y()), enf_max(-Mins.Z(), Maxs.Z()));
	}

/*!
Gets size of AAB. It's distance from
minimum to maximum.
\return Size of AAB
*/
	ENF_INLINE Vector3 GetSize() const
	{
		return (Maxs - Mins);
	}

	ENF_INLINE float GetDiameter() const
	{
#ifdef ENF_POWERPC
		return __fsqrts(sqr(Maxs.x - Mins.x) + sqr(Maxs.y - Mins.y) + sqr(Maxs.z - Mins.z));
#else
		return Mins.Distance(Maxs);
#endif
	}

/*!
Is AAB completely inside
of this AAB.
\param bbox
AAB which we are testing whether is inside of this AAB
\return True when is inside, false otherwise
*/
	bool Inside(const AAB& bbox) const
	{
		return (Mins.X() <= bbox.Mins.X() &&
			Maxs.X() >= bbox.Maxs.X() &&
			Mins._e[1] <= bbox.Mins._e[1] &&
			Maxs._e[1] >= bbox.Maxs._e[1] &&
			Mins._e[2] <= bbox.Mins._e[2] &&
			Maxs._e[2] >= bbox.Maxs._e[2]);
	}

/*!
Is point inside of this AAB.
\param point Point we are testing.
\return True when is inside, false otherwise.
*/
	bool Intersects(const Vector3& point) const
	{
		return !(Mins.X() > point.X() ||
			Mins._e[1] > point._e[1] ||
			Mins._e[2] > point._e[2] ||
			Maxs.X() < point.X() ||
			Maxs._e[1] < point._e[1] ||
			Maxs._e[2] < point._e[2]);
	}

/*!
Is AAB intersecting this AAB.
\param aabb AAB we are testing.
\return True when is intersecting, false otherwise.
*/
	bool Intersects(const AAB& aabb) const
	{
		return !((Mins.X() > aabb.Maxs.X()) ||
			(Mins._e[1] > aabb.Maxs._e[1]) ||
			(Mins._e[2] > aabb.Maxs._e[2]) ||
			(Maxs.X() < aabb.Mins.X()) ||
			(Maxs._e[1] < aabb.Mins._e[1]) ||
			(Maxs._e[2] < aabb.Mins._e[2]));
	}

/*!
Is bounding volume intersecting this AAB.
\param mins
Minimums of volume we are testing.
\param maxs
Maximums of volume we are testing.
\return
True when is intersecting, false otherwise.
*/
	bool Intersects(const Vector3& mins, const Vector3& maxs) const
	{
		return !((Mins.X() > maxs.X()) ||
			(Mins._e[1] > maxs._e[1]) ||
			(Mins._e[2] > maxs._e[2]) ||
			(Maxs.X() < mins.X()) ||
			(Maxs._e[1] < mins._e[1]) ||
			(Maxs._e[2] < mins._e[2]));
	}

/*!
Is bounding volume completely inside, without touching bounds
\param bounds
Volume we are testing.
\return True when touches bounds
*/
	bool Contacts(const AAB& bounds) const
	{
		return (Mins.X() >= bounds.Mins.X() ||
				Mins._e[1] >= bounds.Mins._e[1] ||
				Mins._e[2] >= bounds.Mins._e[2] ||
				Maxs.X() <= bounds.Maxs.X() ||
				Maxs._e[1] <= bounds.Maxs._e[1] ||
				Maxs._e[2] <= bounds.Maxs._e[2]);
	}
/*!
Updates minimums of this AAB in each axis
where point is lesser than minimum bounds
\param point
the point we are extending volume by
\return
Updated AAB
*/
	ENF_INLINE AAB& AddMin(const Vector3& point)
	{
		Mins._e[0] = enf_min(Mins.X(), point.X());
		Mins._e[1] = enf_min(Mins._e[1], point._e[1]);
		Mins._e[2] = enf_min(Mins._e[2], point._e[2]);
		return *this;
	}

/*!
Updates maximums of this AAB in each axis
where point is greater than maximum bounds
\param point The point we are extending volume by
\return Updated AAB
*/
	ENF_INLINE AAB& AddMax(const Vector3& point)
	{
		Maxs._e[0] = enf_max(Maxs.X(), point.X());
		Maxs._e[1] = enf_max(Maxs._e[1], point._e[1]);
		Maxs._e[2] = enf_max(Maxs._e[2], point._e[2]);
		return *this;
	}


/*!
Updates volume of this AAB by extents of
other AAB where maximums are greater than
maximums of this AAB and minimums are
lesser than minimums of this AAB.
\param bbox The AAB we are extending volume by
\return Updated AAB
*/
	ENF_INLINE AAB& Add(const AAB& bbox)
	{
		AddMin(bbox.Mins);
		AddMax(bbox.Maxs);
		return *this;
	}

/*!
Updates volume of this AAB where point axes
are greater than maximums of this AAB
or lesser than minimums of this AAB.
\param point The point we are extending volume by
\return Updated AAB
*/
	ENF_INLINE AAB& Add(const Vector3& point)
	{
		Mins._e[0] = enf_min(Mins._e[0], point.X());
		Mins._e[1] = enf_min(Mins._e[1], point._e[1]);
		Mins._e[2] = enf_min(Mins._e[2], point._e[2]);
		Maxs._e[0] = enf_max(Maxs.X(), point.X());
		Maxs._e[1] = enf_max(Maxs._e[1], point._e[1]);
		Maxs._e[2] = enf_max(Maxs._e[2], point._e[2]);
		return *this;
	}

/*!
Updates volume of this AAB by point array,
where point axes are greater than maximums
of this AAB or lesser than minimums of this
AAB.
\param points Array of points we are extending volume by
\param numpoints Number of points in the array
\param stride optional parameter, when stride of data in array
is different from sizeof(Vector3)
\return Updated AAB
*/
	AAB& AddPoints(const Vector3* points, uint numpoints, uint stride = sizeof(Vector3));

/*!
Updates volume of this AAB where axes
are greater than maximums of this AAB
or lesser than minimums of this AAB.
\param x Position on X axis
\param y Position on Y axis
\param z Position on Z axis
\return
Updated AAB
*/
	ENF_INLINE AAB& Add(float x, float y, float z)
	{
		Add(Vector3(x, y, z));
		return *this;
	}

/*!
Updates volume of this AAB where sphere
is exceeding bounds.
\param point The origin of sphere
\param radius The radius of sphere
\return Updated AAB
*/
	ENF_INLINE AAB& AddSphere(const Vector3& point, float radius)
	{
		AddMin(Vector3(point.X() - radius, point._e[1] - radius, point._e[2] - radius));
		AddMax(Vector3(point.X() + radius, point._e[1] + radius, point._e[2] + radius));
		return *this;
	}

/*!
Updates volume of this AAB by extents of
other AAB (computed from oriented bounding box)
where maximums are greater than
maximums of this AAB and minimums are
lesser than minimums of this AAB.
\param matrix Orientation of AAB
\param bbox The AAB we are extending volume by
\return Updated AAB
*/
	AAB& AddOBB(const Matrix43& matrix, const AAB& bbox);

/*!
Translates whole AAB by offset
\param offset The offset we are translating AAB by
\return Updated AAB
*/
	ENF_INLINE AAB& Translate(const Vector3& offset)
	{
		Mins += offset;
		Maxs += offset;
		return *this;
	}

/*!
Zeroes the AAB. Minimums and maximums
are zero.
\return Updated AAB
*/
	ENF_INLINE AAB& Zero()
	{
		Mins.Zero();
		Maxs.Zero();
		return *this;
	}

/*!
Initializes the AAB. Minimums are set to numeric maximum
and vice versa. The AAB is invalid until some point or volume
is added in.
\return Updated AAB
*/
	ENF_INLINE AAB& Init()
	{
		Mins._e[0] = Mins._e[1] = Mins._e[2] =  9999999.0f;
		Maxs._e[0] = Maxs._e[1] = Maxs._e[2] = -9999999.0f;
		return *this;
	}

/*!
Initializes the AAB. Minimums and maximums are
set to defined extents.
\param mins New minimums
\param maxs New maximums
\return Updated AAB
*/
	ENF_INLINE AAB& Init(const Vector3& mins, const Vector3& maxs)
	{
		Mins = mins;
		Maxs = maxs;
		return *this;
	}

/*!
Initializes the AAB. Minimums and maximums are
set to defined extents.
\param minx New minimums in X axis
\param miny New minimums in Y axis
\param minz New minimums in Z axis
\param maxx New maximums in X axis
\param maxy New maximums in Y axis
\param maxz New maximums in Z axis
\return Updated AAB
*/
	ENF_INLINE AAB& Init(float minx, float miny, float minz, float maxx, float maxy, float maxz)
	{
		Mins._e[0] = minx; Mins._e[1] = miny; Mins._e[2] = minz;
		Maxs._e[0] = maxx; Maxs._e[1] = maxy; Maxs._e[2] = maxz;
		return *this;
	}

/*!
Returns corner based on signs in each axis. Returns maximum
for negative sign and minimum for positive sign
\param x The sign on which is X axis of corner selected
\param y The sign on which is Y axis of corner selected
\param z The sign on which is Z axis of corner selected
\return Selected corner of this AAB
*/
	ENF_INLINE Vector3 GetCornerBySigns(float x, float y, float z) const
	{
		return Vector3(x < 0 ? Maxs._e[0] : Mins._e[0],
							y < 0 ? Maxs._e[1] : Mins._e[1],
							z < 0 ? Maxs._e[2] : Mins._e[2]);
	}

/*!
Initializes the AAB. Minimums and maximums are
both set to defined point.
\param point New minimums and maximums
\return Updated AAB
*/
	ENF_INLINE AAB& Init(const Vector3& point)
	{
		Mins = Maxs = point;
		return *this;
	}

/*!
Initializes the AAB. Minimums and maximums are
defined from trajectory of moving sphere. Could
be used also as bounding volume of line, when
radius is set to zero.
\param from Position where trajectory begins
\param to Position where trajectory ends
\param radius Radius of sphere
\return Updated AAB
*/
	AAB& InitFromMovingSphere(const Vector3& from, const Vector3& to, float radius = 0.0f);


/*!
Initializes the AAB. Minimums and maximums are
defined from trajectory of moving AAB. 
\param from Position where trajectory begins
\param to Position where trajectory ends
\param box AAB of moving box
\param epsilon Optional epsilon by which is resulting bounding box enlarged
\return Updated AAB
*/
	AAB& InitFromMovingBox(const Vector3& from, const Vector3& to, const AAB& box, float epsilon = 0.0f);

/*!
Initializes the AAB. Minimums and maximums are
defined by radius of sphere.
\param radius Radius of sphere
\return Updated AAB
*/
	AAB& InitFromSphere(float radius)
	{
		Mins._e[0] = Mins._e[1] = Mins._e[2] = -radius;
		Maxs._e[0] = Maxs._e[1] = Maxs._e[2] = radius;
		return *this;
	}

/*!
Initializes the AAB. Minimums and maximums are
defined by AAB translated by offset.
\param pos Position where AAB is translated
\param box AAB of box
\param epsilon Optional epsilon by which is resulting bounding box enlarged
\return Updated AAB
*/
	AAB& InitFromBox(const Vector3& pos, const AAB& box, float epsilon = 0.0f)
	{
		Mins = pos + box.Mins;
		Maxs = pos + box.Maxs;

		if(epsilon != 0.0f)
		{
			Mins._e[0] -= epsilon;
			Mins._e[1] -= epsilon;
			Mins._e[2] -= epsilon;
			Maxs._e[0] += epsilon;
			Maxs._e[1] += epsilon;
			Maxs._e[2] += epsilon;
		}
		return *this;
	}

/*!
Initializes the AAB. Minimums and maximums are
defined by oriented AAB.
\param matrix Orientation of AAB
\param box AAB of box
\return Updated AAB
*/
	AAB& InitFromOBB(const Matrix43& matrix, const AAB& box);
	AAB& InitFromOBB(const AAB& box, const Matrix34& matrix);


	float	PlaneDist(const MPlane& pl) const
	{
		float plx = pl.x;
		float ply = pl.y;
		float plz = pl.z;

#ifdef ENF_POWERPC
		float x = __fsel(plx, Maxs.x, Mins.x);
		float y = __fsel(ply, Maxs.y, Mins.y);
		float z = __fsel(plz, Maxs.z, Mins.z);
#else
		float x = (plx < 0) ? Mins._e[0] : Maxs._e[0];
		float y = (ply < 0) ? Mins._e[1] : Maxs._e[1];
		float z = (plz < 0) ? Mins._e[2] : Maxs._e[2];
#endif
		return ((x * plx + y * ply + z * plz) - pl.w);
	}

	float	PlaneDistOptimized(const MPlane& pl) const
	{
		float plx = pl.x;
		float ply = pl.y;
		float plz = pl.z;

#ifdef ENF_POWERPC
		float x = __fsel(plx, Maxs.x, Mins.x);
		float y = __fsel(ply, Maxs.y, Mins.y);
		float z = __fsel(plz, Maxs.z, Mins.z);
#else
		float x = (plx < 0) ? Mins._e[0] : Maxs._e[0];
		float y = (ply < 0) ? Mins._e[1] : Maxs._e[1];
		float z = (plz < 0) ? Mins._e[2] : Maxs._e[2];
#endif
		int n = pl.Type;
		if(n < 0)
		{
			switch(n & 0xff)
			{
			case 0:
				return (x - pl.w);
			case 4:
				return (y - pl.w);
			case 8:
				return (z - pl.w);
			}
		}

		return ((x * plx + y * ply + z * plz) - pl.w);
	}

/*!
Tells how is AAB related to plane or whether is intersected
\return Bit-mask, where 1 means that AAB is inside plane, 2 means
that AAB is outside plane and 3 means that AAB is on both
sides (is crossed by plane).
*/
	ENF_INLINE int OnPlaneOptimized(const MPlane& pl) const
	{
		int type = pl.Type;
		if(type < 0)
		{
			const float* p = (const float *)((const char *)&Mins + (type & 0xff));
			float pw = pl.w;
			int t = (p[3] >= pw) ? 1 : 0;
			t |= (p[0] < pw) ? 2 : 0;

			return t;
		}
		else
		{
#if 1
			float maxx = Maxs._e[0], maxy = Maxs._e[1], maxz = Maxs._e[2];
			float cx = (Mins._e[0] + maxx) * 0.5f;
			float cy = (Mins._e[1] + maxy) * 0.5f;
			float cz = (Mins._e[2] + maxz) * 0.5f;

			float px = pl.x;
			float py = pl.y;
			float pz = pl.z;
			float d2 = fabsf( ( maxx - cx ) * px ) + fabsf( ( maxy - cy ) * py ) + fabsf( ( maxz - cz ) * pz );
			float d1 = (px * cx + py * cy + pz * cz) - pl.w;

			if ( d1 - d2 > 0 )
			{
				return 1;
			}
			if ( d1 + d2 < 0 )
			{
				return 2;
			}
			return 3;

#else
			int t = 0;

			float x1, y1, z1, x2, y2, z2;

			if(pl.x < 0)
			{
				x1 = Mins.x;
				x2 = Maxs.x;
			}
			else
			{
				x1 = Maxs.x;
				x2 = Mins.x;
			}

			if(pl.y < 0)
			{
				y1 = Mins.y;
				y2 = Maxs.y;
			}
			else
			{
				y1 = Maxs.y;
				y2 = Mins.y;
			}

			if(pl.z < 0)
			{
				z1 = Mins.z;
				z2 = Maxs.z;
			}
			else
			{
				z1 = Maxs.z;
				z2 = Mins.z;
			}

			float d1 = pl.x * x1 + pl.y * y1 + pl.z * z1;
			float d2 = pl.x * x2 + pl.y * y2 + pl.z * z2;

			if(d1 >= pl.w)
				t = 1;

			if(d2 < pl.w)
				t |= 2;

			return t;
#endif
		}
	}

	ENF_INLINE int OnPlane(const MPlane& pl) const
	{
#if 1
		float maxx = Maxs._e[0], maxy = Maxs._e[1], maxz = Maxs._e[2];
		float cx = (Mins._e[0] + maxx) * 0.5f;
		float cy = (Mins._e[1] + maxy) * 0.5f;
		float cz = (Mins._e[2] + maxz) * 0.5f;

		float px = pl.x;
		float py = pl.y;
		float pz = pl.z;
		float d2 = fabsf( ( maxx - cx ) * px ) + fabsf( ( maxy - cy ) * py ) + fabsf( ( maxz - cz ) * pz );
		float d1 = (px * cx + py * cy + pz * cz) - pl.w;

		if ( d1 - d2 > 0 )
		{
			return 1;
		}
		if ( d1 + d2 < 0 )
		{
			return 2;
		}
		return 3;

//		int r = ((d1 - d2) > 0) ? 1 : 0;
//		return r | ((d1 + d2) < 0) ? 2 : 0;
#else
		float x1, y1, z1, x2, y2, z2;

		if(pl.x < 0)
		{
			x1 = Mins.x;
			x2 = Maxs.x;
		}
		else
		{
			x1 = Maxs.x;
			x2 = Mins.x;
		}

		if(pl.y < 0)
		{
			y1 = Mins.y;
			y2 = Maxs.y;
		}
		else
		{
			y1 = Maxs.y;
			y2 = Mins.y;
		}

		if(pl.z < 0)
		{
			z1 = Mins.z;
			z2 = Maxs.z;
		}
		else
		{
			z1 = Maxs.z;
			z2 = Mins.z;
		}

		float d1 = pl.x * x1 + pl.y * y1 + pl.z * z1;
		float d2 = pl.x * x2 + pl.y * y2 + pl.z * z2;

		int t = 0;

		if(d1 >= pl.w)
			t = 1;

		if(d2 < pl.w)
			t |= 2;

		return t;
#endif
	}

	ENF_INLINE int OnPlane(int axis, float dist) const
	{
		float max = Maxs._e[axis];
		float c = (Mins._e[axis] + max) * 0.5f;

		float d2 = fabsf( max - c );
		float d1 = c - dist;

		if ( d1 - d2 > 0 )
		{
			return 1;
		}
		if ( d1 + d2 < 0 )
		{
			return 2;
		}
		return 3;
	}

/*!
Tells whether is AAB crossed by plane.
\param plane Normal vector of plane.
\param pdist Distance of plane to zero
\return True when is intersected, false otherwise.
*/
	bool CrossingPlane(const Vector3& plane, float pdist) const
	{
		float x1, y1, z1, x2, y2, z2;

		if(plane._e[0] < 0)
		{
			x1 = Mins._e[0];
			x2 = Maxs._e[0];
		}
		else
		{
			x1 = Maxs._e[0];
			x2 = Mins._e[0];
		}

		if(plane._e[1] < 0)
		{
			y1 = Mins._e[1];
			y2 = Maxs._e[1];
		}
		else
		{
			y1 = Maxs._e[1];
			y2 = Mins._e[1];
		}

		if(plane._e[2] < 0)
		{
			z1 = Mins._e[2];
			z2 = Maxs._e[2];
		}
		else
		{
			z1 = Maxs._e[2];
			z2 = Mins._e[2];
		}

		if((plane._e[0] * x2 + plane._e[1] * y2 + plane._e[2] * z2) >= pdist)
			return false;

		return ((plane._e[0] * x1 + plane._e[1] * y1 + plane._e[2] * z1) >= pdist);
	}

	/*!
	Gets position on, or inside plane which is nearest in each axis to
	the specified point. Useful for example for LOD distance of
	object to camera.
	\param pos Position we are finding nearest position to
	\return the nearest position on box (or inside box, when source point is inside too).
	*/
	Vector3 GetNearestPosition(const Vector3& pos) const
	{
		//get nearest corner of AABB
		return Vector3((pos._e[0] <= Mins._e[0]) ? Mins._e[0] : ((pos._e[0] >= Maxs._e[0]) ? Maxs._e[0] : pos._e[0]),
							(pos._e[1] <= Mins._e[1]) ? Mins._e[1] : ((pos._e[1] >= Maxs._e[1]) ? Maxs._e[1] : pos._e[1]),
							(pos._e[2] <= Mins._e[2]) ? Mins._e[2] : ((pos._e[2] >= Maxs._e[2]) ? Maxs._e[2] : pos._e[2]));
	}

	//!returns vector to nearest position on/in AABB
	Vector3 GetNearestDistance(const Vector3& pos) const
	{
		return Vector3((pos._e[0] <= Mins._e[0]) ? (Mins._e[0] - pos._e[0]) : ((pos._e[0] >= Maxs._e[0]) ? (Maxs._e[0] - pos._e[0]) : 0.0f),
							(pos._e[1] <= Mins._e[1]) ? (Mins._e[1] - pos._e[1]) : ((pos._e[1] >= Maxs._e[1]) ? (Maxs._e[1] - pos._e[1]) : 0.0f),
							(pos._e[2] <= Mins._e[2]) ? (Mins._e[2] - pos._e[2]) : ((pos._e[2] >= Maxs._e[2]) ? (Maxs._e[2] - pos._e[2]) : 0.0f));
	}
};

void __mathInit();

bool Colinear(const Vector3& a, const Vector3& b, const Vector3& c);

/*!
Sets inverse view matrix which corresponds with left-handed projection matrix.
TODO: figure out why must be Z axis flipped. Don't we need RH projection instead?
*/
//Matrix44 SetupViewMatrix(const Matrix43& camera);

int PolyToTriangles(int n, const Vector3 *v, Vector3 *pv );
int PolyToTriangles(const Vector3 *points, uint n, const uint *v, uint *pv );


//-----------------------------------------------------------------------------------
/*!
Static class for linear equation solving 
*/
//-----------------------------------------------------------------------------------
template<class T> class EquationSolver
{
public:
	/*
	Make LUP decomposition of the matrix.
	\param M
	Which matrix to decompose.
	\param LU
	The result of decomposition.
	\param P
	The vector of pivots (each item is the position of row, where the original row was moved).
	\param N
	Size of square matrix (NxN).
	*/
	static bool LUPDecomposition(const T *M, T *LU, uint *P, const uint N);

	/*
	Solve the linear equation of N variables by LUP decomposition, Ax = b.
	\param A
	Matrix of variables.
	\param b
	Vector of right side.
	\param x
	The vector of unknown variables.
	\param N
	Size of square matrix (NxN).
	*/
	static bool Solve(const T *A, const T *b, T *x, const uint N);
};

//typedefs for parameter passing, depends on platform. I.E. some platforms prefer it by value
//because of many register.
//typedef const AAB& AABPar;
//typedef const MPlane& MPlanePar;
//typedef const Matrix44& Matrix44Par;
//typedef const Matrix43& Matrix43Par;
//typedef const Vector3& Vector3Par;
//typedef const Vector4& Vector4Par;

struct PackedVector3
{
	uint	v;

	PackedVector3() {}
	PackedVector3(const PackedVector3& other): v(other.v) {}
	PackedVector3(uint other): v(other) {}
	PackedVector3(const Vector3& other)
	{
		operator=(other);
	}

	PackedVector3& operator=(const Vector3& vec)
	{
		float x = enf_limit(vec._e[0], -1.0f, 1.0f) + 1.0f;
		float y = enf_limit(vec._e[1], -1.0f, 1.0f) + 1.0f;
		float z = enf_limit(vec._e[2], -1.0f, 1.0f) + 1.0f;
		v = ((((int)(x * 1023.0f)) & 0x7ff) << 21) |
			((((int)(y * 1023.0f)) & 0x7ff) << 10) |
			(((int)(z * 511.0f)) & 0x3ff);
		return *this;
	}
	void Zero()
	{
		v = (0x3ff << 21) | (0x3ff << 10) | 0x1ff;
	}
	operator Vector3() const
	{
		float x = ((float)(v >> 21) / 1023.0f) - 1.0f;
		float y = ((float)((v >> 10) & 0x7ff) / 1023.0f) - 1.0f;
		float z = ((float)(v & 0x3ff) / 511.0f) - 1.0f;
		return Vector3(x, y, z);
	}
};

template<class T, int FracBits> struct FixedPoint
{
	T	v;
	FixedPoint() {}
	FixedPoint(const FixedPoint& other): v(other.v) {}
	FixedPoint(uint other): v(other) {}
	FixedPoint(float other)
	{
		operator=(other);
	}

	FixedPoint& operator=(float other)
	{
		v = (T)(other * (float)(1 << FracBits));
		return *this;
	}
	void Zero()
	{
		v = 0;
	}
	operator float() const
	{
		return (float)v / (1 << FracBits);
	}
};

//Matrix33 FromVectorAlongX(const Vector3& vec);
//Matrix33 FromVectorAlongZ(const Vector3& up);

//@}


}//namespace enf

#endif //ENF_MATHLIB_H


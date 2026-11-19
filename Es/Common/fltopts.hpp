#ifndef _FLTOPTS_HPP
#define _FLTOPTS_HPP

#pragma warning( push )
#pragma warning(disable:4530)

#include <limits.h>
#include <math.h>
#include <Es/Framework/debugLog.hpp>

#include <Es/Memory/normalNew.hpp>
#include <algorithm>
#include <Es/Memory/debugNew.hpp>

#pragma warning( pop )

#if _MSC_VER>=1300 && ( _M_IX86_FP>0 || defined _M_X64)
  #define USING_SSE 1
  // TODO: detect SSE2
  #define USING_SSE2 0
#else
  // by default we are not using SSE
  #define USING_SSE 0
  #define USING_SSE2 0
#endif

//@{ int min/max functions

inline int intMax( int a, int b ) {return ( a>b ? a : b);}
inline int intMin( int a, int b ) {return ( a<b ? a : b);}

inline int intMax( int a, int b, int c ) {return intMax(a,intMax(b,c));}
inline int intMin( int a, int b, int c ) {return intMin(a,intMin(b,c));}

inline int intMax( int a, int b, int c, int d ) {return intMax(intMax(a,b),intMax(c,d));}
inline int intMin( int a, int b, int c, int d ) {return intMin(intMin(a,b),intMin(c,d));}

inline int intMinMax(int a, int minVal, int maxVal)
{
	a = intMax(a, minVal);
	a = intMin(a, maxVal);
	return a;
}


inline void saturate( int &a, int min, int max )
{
  if( a<min ) a=min;
  if( a>max ) a=max;
}

inline void saturateMin( int &a, int b ) {if( a>b ) a=b;}
inline void saturateMax( int &a, int b ) {if( a<b ) a=b;}

//@}

#if _MSC_VER && _M_PPC

#include <ppcintrinsics.h>

// double __fsel(double fComparand, double fValGE, double fLT);
// If the operand is less than zero (or is a NaN), register fpDest is set to the contents of register fLT.
//  fComparand >= 0 ? fValGE : fLT

// MS provided max, min is:
//  fpmax(a,b) = __fsel((a)-(b), a,b)   fpmin(a,b) = __fsel((a)-(b), b,a)
// however, we want any NaN in a to be eliminated, therefore we use
//  fpmax(a,b) = __fsel((a)-(b), a,b)   fpmin(a,b) = __fsel((b)-(a), a,b)


// helper functions for complex expressions
// also various tricks to increase speed on Pentium
/// maximum of two values
/** if a is NaN, b is used - helps in saturate */
__forceinline float floatMax(float a, float b) {return __fself(a-b, a,b);}
/// minimum of two values
/** if a is NaN, b is used - helps in saturate */
__forceinline float floatMin(float a, float b) {return __fself(b-a, a,b);}

#else

/// maximum of two values
/** if a is NaN, b is used - helps in saturate */
inline float floatMax(float a, float b) {return ( a>b ? a : b);}
/// minimum of two values
/** if a is NaN, b is used - helps in saturate */
inline float floatMin(float a, float b) {return ( a<b ? a : b);}

#endif

/// if a is above b, saturate it : a = min(a,b)
__forceinline void saturateMin( float &a, float b ) {a = floatMin(a,b);}
/// if a is below b, saturate it : a = max(a,b)
__forceinline void saturateMax( float &a, float b ) {a = floatMax(a,b);}

/// return a saturated between minVal and maxVal
inline float floatMinMax(float a, float minVal, float maxVal)
{
  a = floatMax(a,minVal);
  a = floatMin(a,maxVal);
  return a;
}
/// saturate a between minVal and maxVal
inline void saturate(float &a, float minVal, float maxVal)
{
  a = floatMax(a,minVal);
  a = floatMin(a,maxVal);
}

/// maximum of three values
inline float floatMax(float a, float b, float c) {return floatMax(a,floatMax(b,c));}
/// minimum of three values
inline float floatMin(float a, float b, float c) {return floatMin(a,floatMin(b,c));}

/// maximum of four values
inline float floatMax(float a, float b, float c, float d)
{
  return floatMax(floatMax(a,b),floatMax(c,d));
}
/// minimum of four values
inline float floatMin(float a, float b, float c, float d)
{
  return floatMin(floatMin(a,b),floatMin(c,d));
}


#define saturateAbove saturateMin 
#define saturateBelow saturateMax 

//! Returns logarithm of value rounded down
inline int log2Floor(int value) {
  if (value <= 0) return INT_MIN;
  int counter = -1;
  while (value > 0) {
    counter++;
    value >>= 1;
  }
  return counter;
}

inline int log2Floor(long long value) {
  if (value <= 0) return INT_MIN;
  int counter = -1;
  while (value > 0) {
    counter++;
    value >>= 1;
  }
  return counter;
}

//! Returns logarithm of value rounded up
inline int log2Ceil(int value) {
  int result = log2Floor(value);
  if (result >= 0) {
    int b = 1 << result;
    return (b == value) ? result : result + 1;
  }
  else {
    return result;
  }
}

inline int log2Ceil(long long value) {
  int result = log2Floor(value);
  if (result >= 0) {
    long long b = 1LL << result;
    return (b == value) ? result : result + 1;
  }
  else
  {
    return result;
  }
}

//! Rounds any number to the closest greater number in 2^n form
inline int roundTo2PowerNCeil(int value) {
  saturateMax(value, 1);
  return 1 << log2Ceil(value);
}


//! Rounds any number to the closest greater number in 2^n form
inline long long roundTo2PowerNCeil(long long value) {
  if (value<1) value = 1;
  return 1LL << log2Ceil(value);
}

/// interpolation based on source values, saturated by limits, optimized for constant source values
static __forceinline float InterpolativC
(
  float control, const float cMin, const float cMax,
  float vMin, float vMax
)
{
  if( control<cMin ) return vMin;
  if( control>cMax ) return vMax;
  return (control-cMin)*(1.0f/(cMax-cMin))*(vMax-vMin)+vMin;
}


/** On old CPUs We tried to compute min/max using fabs - on PII with a modern compiler this is no longer efficient */
#define floatMaxFast floatMax
#define floatMinFast floatMin
#define saturateFast saturate

static const float Inv2=0.5;
static const float Snapper=3<<22;

#if USING_SSE
#include <xmmintrin.h>
#endif

/// convert float to int
/**
type-punning to avoid issues with gcc -fstrict-aliasing
*/
union UFloatInt {
 int i;
 float f;
};

#if USING_SSE

__forceinline int toInt( float fval ) {return _mm_cvtss_si32(_mm_set_ss(fval));}
__forceinline int toLargeInt( float fval ) {return _mm_cvtss_si32(_mm_set_ss(fval));}
#else

/**
portable assuming FP24 found to nearest rounding mode
efficient on x86 platform
(by Vlad Kaipetsky <vlad@TEQUE.CO.UK>)
*/
inline int toInt( float fval )
{
#if _DEBUG
  Assert( fabs(fval)<=0x003fffff );
#endif
  UFloatInt &fi = *(UFloatInt *)&fval;
  fi.f += Snapper;
  return ( (fi.i)&0x007fffff ) - 0x00400000;
}

inline int toLargeInt( float f )
{
	int retVal;
	_asm
	{
		fld f;
		fistp retVal;
	}
	return retVal;
}

#endif

#if _MSC_VER && !_MANAGED && !_M_PPC

// TODO: special PowerPC optimized versions
// TODO: verify following code works on PowerPC
inline float fastRound( float x )
{
	// x must be: -1<<21 < x < 1<<21
	volatile float retVal;
	retVal=x+Snapper;
	retVal-=Snapper;
	return retVal;
}

// fast convert float to int - take care to have rounding mode set

/// could be implemented using SSE2 _mm_cvtsd_si64, once SSE2 is required (USING_SSE2)
inline __int64 to64bInt( float f )
{
	__int64 retVal;
	_asm
	{
		fld f;
		fistp retVal;
	}
	return retVal;
}

#else

// portable (non-optimized) version

inline float fastRound( float x ) {return floor(x+0.5f);}
inline int toLargeInt( float x ){return (int)floor(x+0.5f);}
inline __int64 to64bInt( float x ){return (__int64)floor(x+0.5f);}

#endif

/** SSE intrinsics are efficient, but require stack alignment
__forceinline int toInt( float fval ) {return _mm_cvtss_si32(_mm_set_ss(fval));}
*/


inline int toInt( double f ){return toInt(float(f));}

inline int toInt(int i) {return i;}

#define toIntFloor(x) toInt((x)-Inv2)
#define toIntCeil(x) toInt((x)+Inv2)


#define fastCeil(x) fastRound((x)+Inv2)
#define fastFloor(x) fastRound((x)-Inv2)

/// floating point modulo - fast implementation
/** result is positive even for negative x */

inline float fastFmod( float x, const float n )
{ // n is often constant expression
	x*=1/n;
	x-=toIntFloor(x); // nearest int
	return x*n;
}

#if _MSC_VER>1300
// some VS .NET 2003 specific overload to handle int to float propagation
__forceinline int abs(unsigned x)
{
	return abs(int(x));
}

__forceinline float fabs(int x) {return fabs(float(x));}
__forceinline float log(int x) {return log(float(x));}
__forceinline float sqrt(int x) {return sqrt(float(x));}
__forceinline float pow(int x, float y) {return pow(float(x),y);}
//__forceinline float pow(float x, int y) {return pow(x,float(y));}

#endif

#define FP_BITS(fp) (*(DWORD *)&(fp))
#define FP_ABS_BITS(fp) (FP_BITS(fp)&0x7FFFFFFF)
#define FP_SIGN_BIT(fp) (FP_BITS(fp)&0x80000000)
#define FP_ONE_BITS 0x3F800000

// float tricks (by NVIDIA?)

inline float FastInv( float p )
{
	// about 6b precision?
	int _i = 2 * FP_ONE_BITS - *(int *)&(p);
	float r = *(float *)&_i;
	return r * (2.0f - (p) * r);
}

/// Returns the smallest 'y' so that 'x < y'.
inline float nextFloat(float x)
{
  UFloatInt h; h.f = x;

  // next(+inf) = +inf, overflow
  // next(NaN) = error
  if ((h.i & 0x7F800000) == 0x7F800000) { if (h.i != 0xFF800000) return x+x; }  // -inf is handled normally

  // next(-0) = next(0) = smallest subnormal
  else if (h.i == 0x80000000) return 1.401298464e-45f;

  // next(x) = x+ulp;
  if ((h.i & 0x80000000) == 0) h.i++; else h.i--;            
  return h.f;
}


// some tricks to speed-up class handling

template <class Type>
inline void Swap( Type &a, Type &b ){Type p=a;a=b;b=p;}

using std::swap;

//inline void *operator new ( size_t len, void *placement ){return placement;}

inline char myLower( char c )
{
	c+=char(CHAR_MIN-'A');
	if( c<=CHAR_MIN+'Z'-'A' ) c+='a'-'A';
	c-=char(CHAR_MIN-'A');
	return c;
}

inline char myUpper( char c )
{
	c+=char(CHAR_MIN-'a');
	if( c<=CHAR_MIN+'z'-'a' ) c+='A'-'a';
	c-=char(CHAR_MIN-'a');
	return c;
}

/// subtraction dangerous with LARGEADDRESSAWARE
inline int ComparePointerAddresses(const void *a, const void *b)
{
  if (a<b) return -1;
  if (a>b) return +1;
  return 0;
}
// PIII prefetch instructions

// off must be <0x80
// adr should be pointer or reference

//                     0F1800         prefetchnta  BYTE PTR [eax]      1                                   
//                     0F1808         prefetcht0  BYTE PTR [eax]       1                                   
//                     0F184020       prefetchnta  BYTE PTR [eax+020h] 1                                   
//                     0F184040       prefetchnta  BYTE PTR [eax+040h] 1                                   

#if _PIII

/// prefetch to L1 without polluting L2
#define PrefetchNTA(adr) _mm_prefetch(adr,_MM_HINT_NTA)
#define PrefetchNTAOff(adr,off) PrefetchNTA(((char *)adr)+(off))

/// prefetch to both L1 and L2
#define PrefetchT0(adr) _mm_prefetch(adr,_MM_HINT_T0)
#define PrefetchT0Off(adr,off) PrefetchT0(((char *)adr)+(off))

#else

// no prefetch on PII

#define PrefetchNTAOff(adr,off) (void)(adr),(void)(off)
#define PrefetchNTA(adr) (void)(adr)
#define PrefetchT0Off(adr,off) (void)(adr),(void)(off)
#define PrefetchT0(adr) (void)(adr)

#endif

#endif


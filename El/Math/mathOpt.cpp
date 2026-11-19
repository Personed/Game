// homogenous vector and matrix arithmetics
// (C) 1997, SUMA
#include <El/elementpch.hpp>
#include "mathOpt.hpp"
#include <float.h>

#pragma optimize ("t",on)

// note: currently InvSqrt is much slower because of store forwarding
#if _M_PPC

#include <ppcintrinsics.h>
#include <Es\Common\win.h>
//#include <Xboxmath.h>

// X360 optimize 1/sqrt(x)

// frsqrte is 8b only
// vrsqrtefp precision is 12b


float InvSqrt( float x )
{
  XMVECTOR rs = __vrsqrtefp(XMVectorReplicate(x));
  // TODO: use vector here
  float r = rs.x;
  return (3.0f - r * r * x) * r * 0.5f;
}

#elif !USING_SSE

#if TODO
/* following version seems extremely fast and cache friendly
See:
http://www.lomont.org/Math/Papers/2003/InvSqrt.pdf
http://betterexplained.com/articles/understanding-quakes-fast-inverse-square-root/
*/
float InvSqrt(float x)
{
  // caution: would cause LHS on X360
  float xhalf = 0.5f*x;
  int i = *(int*)&x; // get bits for floating value
  i = 0x5f375a86- (i>>1); // gives initial guess y0
  x = *(float*)&i; // convert bits back to float
  x = x*(1.5f-xhalf*x*x); // Newton step, repeating increases accuracy
  return x;
}
#endif

// fast inverse square root
// adapted from Graphics Gems V
// Compute the Inverse Square Root
// of an IEEE Single Precision Floating-Point number.
//
// Written by Ken Turkowski.

// Specified parameters 
// note: LOOKUP_BITS 12 seems to be a little bit faster (1-2% overall)

#if 0
  #define LOOKUP_BITS    6   // Number of mantissa bits for lookup 
  #define SEED_BITS 8 // Number of mantissa bits for seed
  #define SEED_TYPE unsigned char
#else
  #define LOOKUP_BITS    12   // Number of mantissa bits for lookup 
  #define SEED_BITS    16   // Number of mantissa bits for lookup 
  #define SEED_TYPE unsigned short
#endif

#define EXP_POS       23   // Position of the exponent 
#define EXP_BIAS     127   // Bias of exponent 
#define EXP_BITS     8   // Bits of exponent 
// The mantissa is assumed to be just down from the exponent

// Derived parameters
#define SEED_MASK ( (1<<SEED_BITS)-1 )
#define EXP_MASK ( (1<<EXP_BITS)-1 )
#define LOOKUP_POS   (EXP_POS-LOOKUP_BITS)  // Position of mantissa lookup 
#define SEED_POS     (EXP_POS-SEED_BITS)            // Position of mantissa seed 
#define TABLE_SIZE   (2 << LOOKUP_BITS)     // Number of entries in table 
#define LOOKUP_MASK  (TABLE_SIZE - 1)           // Mask for table input 
#define GET_EXP(a)   (((a) >> EXP_POS) & EXP_MASK)  // Extract exponent 
#define SET_EXP(a)   ((a) << EXP_POS)           // Set exponent 
#define GET_EMANT(a) (((a) >> LOOKUP_POS) & LOOKUP_MASK)  // Extended mantissa MSB's 
#define SET_MANTSEED(a) (((unsigned long)(a)) << SEED_POS)  // Set mantissa SEED_BITS MSB's 

class InverseSqrtCalculator
{
  SEED_TYPE _iSqrt[TABLE_SIZE];
  public:
  float Calculate( float x );
  InverseSqrtCalculator();
};

union FloatInt
{
  unsigned long i;
  float f;
};

InverseSqrtCalculator::InverseSqrtCalculator()
{
  long f;
  FloatInt fi,fo;

  SEED_TYPE *h;
  for( f=0,h=_iSqrt; f<TABLE_SIZE; f++)
  {
    fi.i = ((EXP_BIAS-1) << EXP_POS) | (f << LOOKUP_POS);
    fo.f = 1.0 / sqrt(fi.f);
    *h++ = (SEED_TYPE)
        (
            ((fo.i + (1<<(SEED_POS-2))) >> SEED_POS) & SEED_MASK
        ); // rounding
  }
  _iSqrt[TABLE_SIZE / 2] = SEED_MASK;    // Special case for 1.0
}

// The following returns the inverse square root
static InverseSqrtCalculator InvSqrtCalc;

inline float InverseSqrtCalculator::Calculate( float x )
{
  FloatInt fi;
  fi.f=x;
  unsigned long a=fi.i;

  FloatInt seed;
  int exponent=((3*EXP_BIAS-1) - GET_EXP(a))>>1;
  int mantisa=GET_EMANT(a);
  seed.i = SET_EXP(exponent) | SET_MANTSEED(_iSqrt[mantisa]);

  // Seed: accurate to LOOKUP_BITS
  float r = seed.f;
  // First iteration: accurate to 2*LOOKUP_BITS
  r = (3.0 - r * r * x) * r * 0.5;
  #if LOOKUP_BITS<10
    // Second iteration: accurate to 4*LOOKUP_BITS
    r = (3.0 - r * r * x) * r * 0.5;
  #endif
  return r;

  //float er =1/sqrt(x);
  //return er;
}


float InvSqrt( float x )
{
  //float eps=x-1;
  //if( fabs(eps)<=INVSQRT_EPS ) return 1-eps*0.5;
  return InvSqrtCalc.Calculate(x);
}

#else

#include <xmmintrin.h>

#if 1 //_MSC_VER<1300

static const float c3pt0 = 3.0f;
static const float c0pt5 = 0.5f;

#if 1
// this is hand optimized version of the function below
float __declspec(naked) InvSqrt( float x )
{
  __asm
  {
    movss       xmm0,dword ptr [esp+4] 
    movss       xmm2,dword ptr [c3pt0] 
    rsqrtss     xmm1,xmm0 
    mulss       xmm0,xmm1 
    mulss       xmm0,xmm1 
    subss       xmm2,xmm0 
    movss       xmm0,dword ptr [c0pt5] 
    mulss       xmm0,xmm1 
    mulss       xmm0,xmm2 
    // reuse argument as a temporary space
    // this way we can avoid allocating a temporary space
    movss       dword ptr [esp+4],xmm0 
    fld         dword ptr [esp+4] 
    ret              
  }
}
#else
float InvSqrt( float x )
{
  const __m128 c0pt5=_mm_set_ss(0.5f);
  const __m128 c3pt0=_mm_set_ss(3.0f);
  __m128 xx=_mm_set_ss(x);
  __m128 ra0 = _mm_rsqrt_ss(xx);
  __m128 res=_mm_mul_ss
  (
    _mm_mul_ss(c0pt5,ra0),
    _mm_sub_ss(c3pt0,_mm_mul_ss(_mm_mul_ss(xx,ra0),ra0))
  );
  float ret;
  _mm_store_ss(&ret,res);
  return ret;
}

#endif
#else
float InvSqrt( float x )
{
  __m128 xx=_mm_set_ss(x);
  __m128 ra0 = _mm_rsqrt_ss(xx);
  float r;
  _mm_store_ss(&r,ra0);
  r = (3.0f - r * r * x) * r * 0.5f;
  return r;
}
*/

#endif


#endif

float Interpolativ( float control, float cMin, float cMax, float vMin, float vMax )
{
  if( control<=cMin ) return vMin;
  if( control>=cMax ) return vMax;
  return (control-cMin)/(cMax-cMin)*(vMax-vMin)+vMin;
}

float AngleDifference( float a, float b )
{
  float d=a-b;
  if( fabs(d)<=H_PI ) return d;
  d=fastFmod(d,2*H_PI);
  if( d>+H_PI ) d-=2*H_PI;
  if( d<-H_PI ) d+=2*H_PI;
  return d;
}

//#else

#ifdef _MSC_VER

bool VerifyFloat( float x )
{
  switch (_fpclass(x))
  {
    case _FPCLASS_SNAN: case _FPCLASS_QNAN: 
    case _FPCLASS_PINF: 
    case _FPCLASS_NINF: 
      RptF("Non-finite number %g",x);
      return false;
    //case _FPCLASS_PD: case _FPCLASS_ND:
  }
  // all tests on indefinite number will fail
  if (x<=0)
  {
    return true;
  }
  if (x>=0)
  {
    return true;
  }
  // we have indefinite number
  RptF("Invalid number %g",x);
  return false;
}

#else

bool VerifyFloat ( float x )
{
  if ( finite(x) ) return true;
  if ( isinf(x) ) {
    RptF("Non-finite number %g",x);
    return false;
  }
  RptF("Invalid number %g",x);
  return false;
}

#endif

extern const int SNanIEEE32b = 0x7f887f88;

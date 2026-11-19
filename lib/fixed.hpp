/**************************************************************************

		Fixed16p16.h - 16.16 Fixed16p16 point class

**************************************************************************/

#ifndef _FIXED_H
#define _FIXED_H

#ifdef _DEBUG
	#define FIXED_ENUM 0 // select implementation
#else
	#define FIXED_ENUM 0 // select implementation
#endif

// enum is better optimized, class guarantees strict type checking

#pragma warning(disable:4035)               // re-enable below

#ifdef _MSC_VER

inline int fixed_mul( int a, int d )
{
	__asm
	{
		//mov     ecx, 16
		mov     eax, dword ptr [a]
		mov     edx, dword ptr [d]
		imul     edx
		shrd    eax, edx, 16
		//sar     edx, 16
		//nop
	}
}

#else

inline int fixed_mul( int a, int d )
{
    return( (int)(((long long)a * d) >> 16) );
}

#endif

#pragma warning(default:4035)

#if !FIXED_ENUM

// class implementation is syntactically safe, provides exact type checking,
// but a lot of constructors and operators is not well optimized

#define Fixed16p16Val const Fixed16p16 &

class Fixed16p16
{
	protected:
	int fx;

	public:
	enum _directFixed16p16 {Fx};
	
	private:
	
	public:
	__forceinline Fixed16p16() {}
	__forceinline ~Fixed16p16() {}

	// no implicit conversions
	__forceinline Fixed16p16( enum _directFixed16p16, int i ) {fx = i;} // constructors are non-portable - do not use directly
	__forceinline explicit Fixed16p16( long l ) {fx = l<<16;}
	__forceinline explicit Fixed16p16( int i ) {fx = i<<16;}
	__forceinline explicit Fixed16p16( float f ) {fx=toLargeInt(f * 65536.0f);}
	
	__forceinline Fixed16p16( Fixed16p16Val a ) {fx = a.fx;} 
	__forceinline Fixed16p16 &operator = ( Fixed16p16Val a ) {fx = a.fx;return *this;}

	__forceinline friend int fxIntRaw( Fixed16p16Val x ){return x.fx;}

	__forceinline friend int fxToInt( Fixed16p16Val x ) {return x.fx >> 16;}
	__forceinline friend int fxToIntFloor( Fixed16p16Val x ) {return x.fx >> 16;}
	__forceinline friend int fxToIntCeil( Fixed16p16Val x ) {return (int)(x.fx+0xffff) >> 16;}
	__forceinline friend float fxToFloat( Fixed16p16Val x ) {return (float)x.fx*(float)(1.0f/65536.0f);}


	__forceinline Fixed16p16 operator + () const {return Fixed16p16(Fx,fx);}
	__forceinline Fixed16p16 operator - () const {return Fixed16p16(Fx,-fx);}

	__forceinline Fixed16p16 operator + (Fixed16p16Val a) const {return Fixed16p16(Fx,fx + a.fx);}
	__forceinline Fixed16p16 operator - (Fixed16p16Val a) const {return Fixed16p16(Fx,fx - a.fx);}
	__forceinline Fixed16p16 operator * (Fixed16p16Val a) const {return Fixed16p16(Fx,fixed_mul(fx,a.fx));}
	//Fixed16p16 operator / (Fixed16p16 a) const  {return Fixed16p16(Fx,fixed_div(fx,a.fx));}

	__forceinline bool operator <( Fixed16p16Val a ) const  {return fx < a.fx;}
	__forceinline bool operator >( Fixed16p16Val a ) const  {return fx > a.fx;}
	__forceinline bool operator <=( Fixed16p16Val a ) const  {return fx <= a.fx;}
	__forceinline bool operator >=( Fixed16p16Val a ) const  {return fx >= a.fx;}
	__forceinline bool operator ==( Fixed16p16Val a ) const {return fx == a.fx;}
	__forceinline bool operator !=( Fixed16p16Val a ) const {return fx != a.fx;}

	__forceinline Fixed16p16& operator +=( Fixed16p16Val a ) {fx += a.fx; return *this;}
	__forceinline Fixed16p16& operator -=( Fixed16p16Val a ) {fx -= a.fx; return *this;}
	__forceinline Fixed16p16& operator *=( Fixed16p16Val a ) {fx  = fixed_mul(fx,a.fx); return *this;}

	// shift operators
	__forceinline Fixed16p16 operator <<( int a ) const {return Fixed16p16(Fx,fx<<a);}
	__forceinline Fixed16p16 operator >>( int a ) const {return Fixed16p16(Fx,fx>>a);}

	__forceinline void operator <<=( int a ) {fx<<=a;}
	__forceinline void operator >>=( int a ) {fx>>=a;}
	
	// optimize mul/div by integer
	__forceinline Fixed16p16 operator *( int a ) const {return Fixed16p16(Fx,fx*a);}
	__forceinline Fixed16p16 operator /( int a ) const {return Fixed16p16(Fx,fx/a);}
	__forceinline Fixed16p16& operator *=( int a ) {fx*=a; return *this;}
	__forceinline Fixed16p16& operator /=( int a ) {fx/=a; return *this;}

	__forceinline Fixed16p16 operator *( float a ) const {return Fixed16p16(Fx,toLargeInt(fx*a));}
	
	//friend int &operator *= ( int &a, Fixed16p16Val b );
};

#define FIXED_MAX Fixed16p16(Fixed16p16::Fx,0x7fffffff)
#define FIXED_MIN Fixed16p16(Fixed16p16::Fx,-0x80000000)

// standard way to call explicit conversions:
__forceinline Fixed16p16 fixed( int x ) {return Fixed16p16(x);}
__forceinline Fixed16p16 fixed( float x ) {return Fixed16p16(x);}
__forceinline Fixed16p16 fixed( Fixed16p16Val x ) {return x;}

static Fixed16p16 Fixed16p160(0);

/*
__forceinline Fixed16p16 abs( Fixed16p16Val a )
{
	if( a>=Fixed16p160 ) return a;
	else return -a;
}
*/

#include "math.h"

#else

// enum implementation is very fast and simple

typedef enum _fixed
{
	Fixed16p160=0,
	FIXED_MAX=0x7fffffffL,
	FIXED_MIN=-(0x80000000L),
} Fixed16p16;


// no implicit conversions

__forceinline  Fixed16p16 fixed( long  l ){return Fixed16p16(l<<16);}
__forceinline  Fixed16p16 fixed( int  i ){return Fixed16p16(i<<16);}
__forceinline  Fixed16p16 fixed( Fixed16p16  i ){return i;}
__forceinline  Fixed16p16 fixed( double  d ){int fx=toLargeInt(d * 65536.0);return (Fixed16p16)fx;}
__forceinline  Fixed16p16 fixed( float  d ){int fx=toLargeInt(d * (float)65536.0);return (Fixed16p16)fx;}
	
__forceinline  int fxToInt( Fixed16p16 fx ){return (int)fx >> 16;}
__forceinline  int fxToIntFloor( Fixed16p16 fx ){return (int)fx >> 16;}
__forceinline  int fxToIntCeil( Fixed16p16 fx ){return (int)(fx+0xffff) >> 16;}

__forceinline  int fxIntRaw( Fixed16p16 fx ){return fx;}

__forceinline  float toFloat( Fixed16p16 fx ){return (float)(int)fx/(float)(1.0/65536.0);}
__forceinline  Fixed16p16 frac( Fixed16p16 fx ){return Fixed16p16((int)(unsigned short)fx);}
__forceinline  Fixed16p16 isFixed16p16( int x ) {return Fixed16p16(x);}
//inline int Fixed16p16AsInt( Fixed16p16 x ) {return (int)x;}

__forceinline  Fixed16p16 operator + ( Fixed16p16 a ) {return a;}
__forceinline  Fixed16p16 operator - ( Fixed16p16 a )  {return Fixed16p16(-(int)a);}

/*
__forceinline  Fixed16p16 abs( Fixed16p16 a )
{
	if( (int)a>=0 ) return a;
	else return (Fixed16p16)-(int)a;
}
*/

__forceinline  Fixed16p16 operator + ( Fixed16p16 a, Fixed16p16 b ) {return Fixed16p16( (int)a+(int)b );}
__forceinline  Fixed16p16 operator - ( Fixed16p16 a, Fixed16p16 b ) {return Fixed16p16( (int)a-(int)b );}
__forceinline  Fixed16p16 operator * (Fixed16p16 a, Fixed16p16 b ) {return Fixed16p16( fixed_mul((int)a,(int)b) );}
//inline Fixed16p16 operator / (Fixed16p16 a, Fixed16p16 b ) {return Fixed16p16( fixed_div((int)a,(int)b) );}

// usuall comparison operators use unsigned values

__forceinline  bool operator <( Fixed16p16 a, Fixed16p16 b )  {return (int)a<(int)b;}
__forceinline  bool operator >( Fixed16p16 a, Fixed16p16 b )  {return (int)a>(int)b;}
__forceinline  bool operator <=( Fixed16p16 a, Fixed16p16 b )  {return (int)a<=(int)b;}
__forceinline  bool operator >=( Fixed16p16 a, Fixed16p16 b )  {return (int)a>=(int)b;}
// operators == and != work fine (no difference between singed and unsigned version)

__forceinline  Fixed16p16 &operator +=( Fixed16p16 &a, Fixed16p16 b ) {a=a+b;return a;}
__forceinline  Fixed16p16 &operator -=( Fixed16p16 &a, Fixed16p16 b ) {a=a-b;return a;}
__forceinline  Fixed16p16 &operator *=( Fixed16p16 &a, Fixed16p16 b ) {a=a*b;return a;}
//inline Fixed16p16 &operator /=( Fixed16p16 &a, Fixed16p16 b ) {a=a/b;return a;}

// shift operators
__forceinline  Fixed16p16 operator <<( Fixed16p16 fx, int a ) {return Fixed16p16((int)fx<<a);}
__forceinline  Fixed16p16 operator >>( Fixed16p16 fx, int a ) {return Fixed16p16((int)fx>>a);}

__forceinline  Fixed16p16 &operator <<=( Fixed16p16 &fx, int a ) {fx=Fixed16p16((int)fx<<a);return fx;}
__forceinline  Fixed16p16 &operator >>=( Fixed16p16 &fx, int a ) {fx=Fixed16p16((int)fx>>a);return fx;}
					
// optimize mul/div by integer
__forceinline  Fixed16p16 operator *( Fixed16p16 fx, int a ) {return Fixed16p16((int)fx*a);}
__forceinline  Fixed16p16 operator /( Fixed16p16 fx, int a ) {return Fixed16p16((int)fx/a);}
__forceinline  Fixed16p16& operator *=( Fixed16p16 &fx, int a ) {fx=Fixed16p16((int)fx*a); return fx;}
__forceinline  Fixed16p16& operator /=( Fixed16p16 &fx, int a ) {fx=Fixed16p16((int)fx/a); return fx;}


__forceinline Fixed16p16 operator *( Fixed16p16 fx, float a ) {return Fixed16p16(toLargeInt((int)fx*a));}

#endif

#endif


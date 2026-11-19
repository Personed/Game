#pragma once
// Dx11MathCompat.hpp
// Drop-in replacements for D3DX10math.h types used in the DX11 engine code.
// Provides D3DXVECTOR2/3/4, D3DXMATRIX, D3DXMATRIXA16 as simple POD structs
// with the same layout and constructors. This eliminates the dependency on
// the deprecated D3DX10math.h header and d3dx10_41.dll.
//
// No D3DX math *functions* (D3DXMatrixMultiply, etc.) are used in the DX11
// backend; only the struct types and their constructors are needed.

#ifndef _DX11_MATH_COMPAT_HPP
#define _DX11_MATH_COMPAT_HPP

#include <cmath>
#include <cstring>

// Legacy D3DX shader flag; no longer meaningful with D3DCompile but harmless.
#ifndef D3DXSHADER_USE_LEGACY_D3DX9_31_DLL
#define D3DXSHADER_USE_LEGACY_D3DX9_31_DLL 0
#endif

// ---------------------------------------------------------------------------
// D3DXVECTOR2
// ---------------------------------------------------------------------------
struct D3DXVECTOR2
{
  float x, y;

  D3DXVECTOR2() : x(0), y(0) {}
  D3DXVECTOR2(float _x, float _y) : x(_x), y(_y) {}

  D3DXVECTOR2 operator+(const D3DXVECTOR2 &v) const { return { x + v.x, y + v.y }; }
  D3DXVECTOR2 operator-(const D3DXVECTOR2 &v) const { return { x - v.x, y - v.y }; }
  D3DXVECTOR2 operator*(float s) const { return { x * s, y * s }; }

  operator float *() { return &x; }
  operator const float *() const { return &x; }
};

// ---------------------------------------------------------------------------
// D3DXVECTOR3
// ---------------------------------------------------------------------------
struct D3DXVECTOR3
{
  float x, y, z;

  D3DXVECTOR3() : x(0), y(0), z(0) {}
  D3DXVECTOR3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

  D3DXVECTOR3 operator+(const D3DXVECTOR3 &v) const { return { x + v.x, y + v.y, z + v.z }; }
  D3DXVECTOR3 operator-(const D3DXVECTOR3 &v) const { return { x - v.x, y - v.y, z - v.z }; }
  D3DXVECTOR3 operator*(float s) const { return { x * s, y * s, z * s }; }

  operator float *() { return &x; }
  operator const float *() const { return &x; }
};

// ---------------------------------------------------------------------------
// D3DXVECTOR4
// ---------------------------------------------------------------------------
struct D3DXVECTOR4
{
  float x, y, z, w;

  D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
  D3DXVECTOR4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

  D3DXVECTOR4 operator+(const D3DXVECTOR4 &v) const { return { x + v.x, y + v.y, z + v.z, w + v.w }; }
  D3DXVECTOR4 operator-(const D3DXVECTOR4 &v) const { return { x - v.x, y - v.y, z - v.z, w - v.w }; }
  D3DXVECTOR4 operator*(float s) const { return { x * s, y * s, z * s, w * s }; }

  operator float *() { return &x; }
  operator const float *() const { return &x; }
};

// ---------------------------------------------------------------------------
// D3DMATRIX / D3DXMATRIX - 4x4 row-major matrix
// ---------------------------------------------------------------------------
#ifndef D3DMATRIX_DEFINED
typedef struct _D3DMATRIX
{
  union
  {
    struct
    {
      float _11, _12, _13, _14;
      float _21, _22, _23, _24;
      float _31, _32, _33, _34;
      float _41, _42, _43, _44;
    };
    float m[4][4];
  };
} D3DMATRIX;
#define D3DMATRIX_DEFINED
#endif

struct D3DXMATRIX : public D3DMATRIX
{
  D3DXMATRIX()
  {
    memset(this, 0, sizeof(*this));
  }

  D3DXMATRIX(
    float m11, float m12, float m13, float m14,
    float m21, float m22, float m23, float m24,
    float m31, float m32, float m33, float m34,
    float m41, float m42, float m43, float m44)
  {
    _11 = m11; _12 = m12; _13 = m13; _14 = m14;
    _21 = m21; _22 = m22; _23 = m23; _24 = m24;
    _31 = m31; _32 = m32; _33 = m33; _34 = m34;
    _41 = m41; _42 = m42; _43 = m43; _44 = m44;
  }

  D3DXMATRIX(const D3DMATRIX &mat)
  {
    memcpy(this, &mat, sizeof(D3DMATRIX));
  }

  D3DXMATRIX operator*(const D3DXMATRIX &rhs) const
  {
    D3DXMATRIX res;
    for (int r = 0; r < 4; ++r)
    {
      for (int c = 0; c < 4; ++c)
      {
        res.m[r][c] =
          m[r][0] * rhs.m[0][c] +
          m[r][1] * rhs.m[1][c] +
          m[r][2] * rhs.m[2][c] +
          m[r][3] * rhs.m[3][c];
      }
    }
    return res;
  }

  operator float *() { return &_11; }
  operator const float *() const { return &_11; }

  float &operator()(int row, int col) { return m[row][col]; }
  float operator()(int row, int col) const { return m[row][col]; }
};

// D3DXMATRIXA16 - 16-byte aligned matrix (Xbox specialization; on PC same as D3DXMATRIX)
typedef __declspec(align(16)) D3DXMATRIX D3DXMATRIXA16;

// ---------------------------------------------------------------------------
// D3DXCOLOR
// ---------------------------------------------------------------------------
struct D3DXCOLOR
{
  float r, g, b, a;

  D3DXCOLOR() : r(0), g(0), b(0), a(0) {}
  D3DXCOLOR(float _r, float _g, float _b, float _a) : r(_r), g(_g), b(_b), a(_a) {}

  operator float *() { return &r; }
  operator const float *() const { return &r; }
};

// ---------------------------------------------------------------------------
// D3DXPLANE
// ---------------------------------------------------------------------------
struct D3DXPLANE
{
  float a, b, c, d;

  D3DXPLANE() : a(0), b(0), c(0), d(0) {}
  D3DXPLANE(float _a, float _b, float _c, float _d) : a(_a), b(_b), c(_c), d(_d) {}

  operator float *() { return &a; }
  operator const float *() const { return &a; }
};

// ---------------------------------------------------------------------------
// D3DXQUATERNION
// ---------------------------------------------------------------------------
struct D3DXQUATERNION
{
  float x, y, z, w;

  D3DXQUATERNION() : x(0), y(0), z(0), w(0) {}
  D3DXQUATERNION(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

  operator float *() { return &x; }
  operator const float *() const { return &x; }
};

#endif // _DX11_MATH_COMPAT_HPP

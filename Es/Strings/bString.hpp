#ifdef _MSC_VER
#pragma once
#endif

#ifndef _BSTRING_HPP
#define _BSTRING_HPP

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Es/Framework/debugLog.hpp>
#include <Es/Files/filenames.hpp>

class LString;

__forceinline void strcpyLtd(char *dst, const char *src, size_t size)
{
  if (size>0) strncpy(dst,src,size-1);
}

__forceinline void strcatLtd(char *dst, const char *src, size_t size)
{
  while (size>0 && *dst)
  {
    dst++;
    size--;
  }
  strcpyLtd(dst,src,size);
}

//! template for automatic character buffer limitation handling
template <int Size>
class BString
{
  char _data[Size];
  
  //! safe copy from raw string
  void Copy(const char *src)
  {
    strcpyLtd(_data,src,sizeof(_data));
  }

  public:
  BString()
  {
    _data[0]=0;
    // note: last character will always stay zero
    // if not, overflow occurred
    // all constructors should contain last element initialization
    // so that it can be verified during destruction
    _data[Size-1]=0;
  }
  ~BString()
  {
    Assert(IsValid());
  }

  //! check is string is still valid (last char must be zero)
  bool IsValid() const
  {
    if (_data[sizeof(_data)-1])
    {
      Fail("BString overflow.");
      return false;
    }
    return true;
  }

  //! conversion to const char *
  operator const char *() const {return _data;}
  //! conversion to const char *
  const char *cstr() const {return _data;}

  /// pointer arithmetics - return guarder buffer
  LString operator + (int index);

  LString operator + (size_t index);
  
  //! conversion from const char *
  BString(const char *src)
  {
    Copy(src);
    _data[Size-1]=0;
  }
  //! conversion from const char *
  const BString &operator = (const char *src)
  {
    Copy(src);
    return *this;
  }

  //! array-like access
  char &operator [] (int i)
  {
    Assert(i>=0);
    // we cannot write a last character
    Assert(i<sizeof(_data)-1);
    return _data[i];
  }
  //! array-like access
  const char &operator [] (int i) const
  {
    Assert(i>=0);
    Assert(i<sizeof(_data));
    return _data[i];
  }

  //! simple case assignment: no overflow guaranteed
  const BString &operator = (const BString &src)
  {
    strcpy(_data,src._data);
    return *this;
  }
  //! simple case assignment: no overflow guaranteed
  BString(const BString &src)
  {
    strcpy(_data,src._data);
    _data[Size-1]=0;
  }
  //! string concatenation
  const BString &operator += (const char *src)
  {
    strcatLtd(_data,src,sizeof(_data));
    return *this;
  }
  //! sprintf replacement
  int PrintF(const char *format, ...)
  {
    va_list va;
    va_start(va,format);
    int ret = vsnprintf(_data,sizeof(_data)-1,format,va);
    va_end(va);
    return ret;
  }
  //! vsprintf replacement
  int VPrintF(const char *format, va_list va)
  {
    return vsnprintf(_data,sizeof(_data)-1,format,va);
  }
  //! strncpy replacement
  const BString &StrNCpy(const char *src, int n)
  {
    if (n<sizeof(_data)-1)
    {
      strncpy(_data,src,n);
    }
    else
    {
      strcpyLtd(_data,src,sizeof(_data));
    }
    return *this;
  }
  BString &StrLwr()
  {
    strlwr(_data);
    return *this;
  }
  BString &StrUpr()
  {
    strupr(_data);
    return *this;
  }
  BString &UnixPath()
  {
    unixPath(_data);
    return *this;
  }

};

//! overloads that make conversion of C code easier 
template <int Size>
inline const BString<Size> & strcpy(BString<Size> &dst, const char *src)
{
  return dst = src;
}

//! overloads that make conversion of C code easier 
template <int Size>
inline const BString<Size> & strncpy(BString<Size> &dst, const char *src, int n)
{
  dst.StrNCpy(src,n);
  return dst;
}

//! overloads that make conversion of C code easier 
template <int Size>
inline const BString<Size> & strcat(BString<Size> &dst, const char *src)
{
  return dst += src;
}

//! overloads that make conversion of C code easier 
template <int Size>
inline BString<Size> & strlwr(BString<Size> &dst)
{
  return dst.StrLwr();
}

//! overloads that make conversion of C code easier 
template <int Size>
inline BString<Size> & strupr(BString<Size> &dst)
{
  return dst.StrUpr();
}

//! overloads that make conversion of C code easier 
template <int Size>
inline int sprintf(BString<Size> &dst, const char *format, ...)
{
  va_list va;
  va_start(va,format);
  int ret = dst.VPrintF(format,va);
  va_end(va);
  return ret;
}

template <int Size>
inline int vsprintf(BString<Size> &dst, const char *format, va_list argptr)
{
  return dst.VPrintF(format,argptr);
}

/// convert to human readable format
template <int Size>
void FormatByteSize(BString<Size> &dst, size_t size)
{
  const int oneKB = 1024;
  const int oneMB = 1024*1024;
  const int oneGB = 1024*1024*1024;
  if (size<1000)
  {
    sprintf(dst,"%d B",size);
  }
  else if (size<10*oneKB-oneKB/2)
  {
    sprintf(dst,"%.1f KB",size/float(oneKB));
  }
  else if (size<1000*oneKB-oneKB/2)
  {
    sprintf(dst,"%.0f KB",size/float(oneKB));
  }
  else if (size<10*oneMB-oneMB/2)
  {
    sprintf(dst,"%.1f MB",size/float(oneMB));
  }
  else if (size<1000*oneMB-oneMB/2)
  {
    sprintf(dst,"%.0f MB",size/float(oneMB));
  }
  #if 0
  // following code makes sense only for 64b platforms
  else if (size<10*oneGB-oneGB/2)
  {
    sprintf(dst,"%.1f GB",size/float(oneGB));
  }
  else
  {
    sprintf(dst,"%.0f GB",size/float(oneGB));
  }
  #else
  else
  {
    // with 32b size 
    sprintf(dst,"%.1f GB",size/float(oneGB));
  }
  #endif
}

/// limited string
/**
Similar to BString, but constraint is runtime, not compile time
*/
class LString
{
  char *_data;
  size_t _size;
  
  public:
  LString():_data(NULL),_size(0){}
  
  template <int size>
  LString(BString<size> &src)
  {
    _data = unconst_cast(src.cstr());
    _size = size;
  }
  LString(char *data, size_t size)
  :_data(data),_size(size)
  {
  }
  
  operator const char *() const {return _data;}
  
  //! array-like access
  char &operator [] (int i)
  {
    Assert(i>=0);
    // we cannot write a last character
    Assert(size_t(i)<_size);
    return _data[i];
  }
  //! array-like access
  const char &operator [] (int i) const
  {
    Assert(i>=0);
    Assert(size_t(i)<=_size);
    return _data[i];
  }
  
  LString operator + (ptrdiff_t offset) const
  {
    if (offset>=(ptrdiff_t)_size)
    {
      // no buffer left, no data to fill
      return LString();
    }
    else
    {
      return LString(_data+offset,_size-offset);
    }
  }
  LString operator + (size_t index) const
  {
    return operator + (ptrdiff_t(index));
  }
  
  const LString &operator = (const char *src) const
  {
    strcpyLtd(_data,src,_size);
    return *this;
  }
  //! string concatenation
  const LString &operator += (const char *src)
  {
    strcatLtd(_data,src,_size);
    return *this;
  }
  
  /// skip a few characters, like CString::Mid
  LString Mid(size_t index) const
  {
    Assert(index>=0 && index<_size);
    return LString(_data+index,_size-index);
  }
  /// skip all characters to start
  LString Mid(const char *start) const
  {
    return Mid(start-_data);
  }
  //! sprintf replacement
  int PrintF(const char *format, ...)
  {
    va_list va;
    va_start(va,format);
    int ret = vsnprintf(_data,_size-1,format,va);
    va_end(va);
    return ret;
  }
  //! vsprintf replacement
  int VPrintF(const char *format, va_list va)
  {
    return vsnprintf(_data,_size-1,format,va);
  }

  LString Find(char c) const
  {
    const char *find = strchr(_data,c);
    if (!find) find = strchr(_data,0);
    return LString(_data+(find-_data),_size-(find-_data));
  }

  LString FindR(char c) const 
  {
    const char *find = strrchr(_data,c);
    if (!find) find = _data;
    return LString(_data+(find-_data),_size-(find-_data));
  }
};


/**
no const here - we want to use this only when destination can be modified */

template <int Size>
LString BString<Size>::operator + (int index)
{
  return LString(*this)+index;
}

template <int Size>
LString BString<Size>::operator + (size_t index)
{
  return LString(*this)+index;
}

inline const LString &strcat(LString &tgt, const char *src) {tgt += src;return tgt;}
inline const LString &strcpy(const LString &tgt, const char *src) {tgt = src;return tgt;}

inline LString strchr(LString &src, char c) {return src.Find(c);}
inline LString strrchr(LString &src, char c) {return src.FindR(c);}

template <int Size> inline LString strchr(BString<Size> &src, char c) {return LString(src).Find(c);}
template <int Size> inline LString strrchr(BString<Size> &src, char c) {return LString(src).FindR(c);}

//! overloads that make conversion of C code easier 
inline int sprintf(LString &dst, const char *format, ...)
{
  va_list va;
  va_start(va,format);
  int ret = dst.VPrintF(format,va);
  va_end(va);
  return ret;
}

inline int vsprintf(LString &dst, const char *format, va_list argptr)
{
  return dst.VPrintF(format,argptr);
}

inline LString GetFilenameExt( const LString &w )
{ // short name with extension
  const char *nam = strrchr(w,'\\');
  if( nam )
  {
    return w.Mid(nam+1);
  }
  if( w[0]!=0 && w[1]==':' )
  {
    return w.Mid(2);
  }
  return w;
}

inline LString GetFileExt( const LString &w )
{ // extension including leading point
  LString filename = GetFilenameExt(w);
  const char *ext=strrchr(filename,'.');
  if( ext ) return w.Mid(ext);
  return w.Mid(strlen(w));
}

/// short name with extension - BString overload
template <int size>
inline LString GetFilenameExt( BString<size> &w )
{

  LString str(w);
  return GetFilenameExt(str);
}

/// extension - BString overload
template <int size>
inline LString GetFileExt( BString<size> &w )
{ // short name with extension
  LString str(w);
  return GetFileExt(str);
}

template <int size>
inline void TerminateBy( BString<size> &s, int c )
{ // if c is not last character, add it
  int len = strlen(s);
  if ((len<=0 || s[len-1]!=c) && len<size-1)
  {
    s[len] = c;
    s[len+1] = 0;
  }
}

#endif


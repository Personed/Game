// should be included in source file with #pragma init_seg(lib)
// to guarantee initialization before all user variables

#pragma warning(disable:4073)
#pragma init_seg(lib)

#include <Es/essencepch.hpp>
#include <Es/Strings/rString.hpp>

#include <stdarg.h>
#include <stdio.h>

template<> bool RStringCT<char>::IsLowCase(const char *string)
{
  while (*string)
  {
    char c = *string++;
    if (myLower(c)!=c) return false;
  }
  return true;
}

template<> bool RStringCT<char>::IsLowCase() const
{
  int len=GetLength();
  for (int i=0; i<len; i++)
  {
    int c = _ref->Data()[i];
    if (myLower(c)!=c) return false;
  }
  return true;
}

#if 1
RStringBank StringBank INIT_PRIORITY_HIGH; // global string bank
RStringB RStringBEmpty INIT_PRIORITY_HIGH ="";
#endif

#if 1
RStringBankI StringBankI INIT_PRIORITY_HIGH; // global string bank
RStringIB RStringIBEmpty INIT_PRIORITY_HIGH ="";
#endif

RString FormatNumber(int number)
{
	char buf[256];
	snprintf(buf,sizeof(buf)-1,"%d",number);
	buf[sizeof(buf)-1]=0;
	return buf;
}

RString Format(const char *format, ...)
{
	char buf[1024];
	va_list va;
	va_start(va,format);
	vsnprintf(buf,sizeof(buf)-1,format,va);
	buf[sizeof(buf)-1]=0;
	va_end(va);
	return buf;
}

RString FormatByteSize(size_t size)
{
  const int oneKB = 1024;
  const int oneMB = 1024*1024;
  const int oneGB = 1024*1024*1024;
	if (size<1000)
	{
		return Format("%d B",size);
	}
	else if (size<10*oneKB-oneKB/2)
	{
		return Format("%.1f KB",size/float(oneKB));
	}
	else if (size<1000*oneKB-oneKB/2)
	{
		return Format("%.0f KB",size/float(oneKB));
	}
	else if (size<10*oneMB-oneMB/2)
	{
		return Format("%.1f MB",size/float(oneMB));
	}
	else if (size<1000*oneMB-oneMB/2)
	{
  	return Format("%.0f MB",size/float(oneMB));
	}
	#if 0
	// following code makes sense only for 64b platforms
	else if (size<10*oneGB-oneGB/2)
	{
    return Format("%.1f GB",size/float(oneGB));
  }
  else
  {
    return Format("%.0f GB",size/float(oneGB));
  }
  #else
	else
	{
	  // with 32b size 
    return Format("%.1f GB",size/float(oneGB));
  }
  #endif
  
}

RString FormatByteSize(unsigned long long size)
{
  const int oneKB = 1024;
  const int oneMB = 1024*1024;
  const int oneGB = 1024*1024*1024;
	if (size<1000)
	{
		return Format("%d B",size);
	}
	else if (size<10*oneKB-oneKB/2)
	{
		return Format("%.1f KB",size/float(oneKB));
	}
	else if (size<1000*oneKB-oneKB/2)
	{
		return Format("%.0f KB",size/float(oneKB));
	}
	else if (size<10*oneMB-oneMB/2)
	{
		return Format("%.1f MB",size/float(oneMB));
	}
	else if (size<1000*oneMB-oneMB/2)
	{
  	return Format("%.0f MB",size/float(oneMB));
	}
	// following code makes sense only for 64b platforms
	else if (size<10LL*oneGB-oneGB/2)
	{
    return Format("%.1f GB",size/float(oneGB));
  }
  else
  {
    return Format("%.0f GB",size/float(oneGB));
  }
}

RString FormatByteSize(long long size)
{
  long long magSize = size>0 ? size : -size; 
  const int oneKB = 1024;
  const int oneMB = 1024*1024;
  const int oneGB = 1024*1024*1024;
  if (magSize<1000)
  {
    return Format("%d B",size);
  }
  else if (magSize<10*oneKB-oneKB/2)
  {
    return Format("%.1f KB",size/float(oneKB));
  }
  else if (magSize<1000*oneKB-oneKB/2)
  {
    return Format("%.0f KB",size/float(oneKB));
  }
  else if (magSize<10*oneMB-oneMB/2)
  {
    return Format("%.1f MB",size/float(oneMB));
  }
  else if (magSize<1000*oneMB-oneMB/2)
  {
    return Format("%.0f MB",size/float(oneMB));
  }
  // following code makes sense only for 64b platforms
  else if (magSize<10LL*oneGB-oneGB/2)
  {
    return Format("%.1f GB",size/float(oneGB));
  }
  else
  {
    return Format("%.0f GB",size/float(oneGB));
  }
}

#ifndef _WIN32
inline void addCode(int c, char *buf, int bufLen, int &ix)
{
  if (ix<bufLen)
  {
    buf[ix]=c;
  }
  ix++;
}

static int WideCharToUTF8 (const wchar_t *text, int wlen, char *buf, int bufLen)
{
  int ix=0;
  int len = wlen<0 ? wcslen(text)+1 : wlen;
  for(int i=0; i<len; i++) 
  {
    unsigned int code = text[i]; //Unicode
    if (code < 0x00000080) addCode(code, buf, bufLen, ix);
    else if (code < 0x00000800) 
    {
      addCode(((code>>06)&0x1F)+0xC0, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x00010000)
    {
      addCode(((code>>12)&0x0F)+0xE0, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x00200000)
    {
      addCode(((code>>18)&0x07)+0xF0, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x04000000)
    {
      addCode(((code>>24)&0x03)+0xF8, buf, bufLen, ix);
      addCode(((code>>18)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else
    {
      addCode(((code>>30)&0x01)+0xFC, buf, bufLen, ix);
      addCode(((code>>24)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>18)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
  }
  return ix;
}

int UTF16ToUTF8 (const char *text, int len, char *buf, int bufLen)
{
  int wLen = len/2;
  int ix = 0;
  const unsigned short *u16char = (const unsigned short *)text;
  for (int i=0; i<wLen; i++)
  {
    unsigned int codepoint = u16char[i]; //Unicode
    // convert to UTF32 first
    // constants
    const unsigned int LEAD_OFFSET = 0xD800 - (0x10000 >> 10);
    const unsigned int SURROGATE_OFFSET = 0x10000 - (0xD800 << 10) - 0xDC00;
    // computations
    unsigned short lead = LEAD_OFFSET + (codepoint >> 10);
    unsigned short trail = 0xDC00 + (codepoint & 0x3FF);
    unsigned int code = (lead << 10) + trail + SURROGATE_OFFSET;
    // code now contains UTF32 code
    if (code < 0x00000080) addCode(code, buf, bufLen, ix);
    else if (code < 0x00000800) 
    {
      addCode(((code>>06)&0x1F)+0xC0, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x00010000)
    {
      addCode(((code>>12)&0x0F)+0xE0, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x00200000)
    {
      addCode(((code>>18)&0x07)+0xF0, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else if (code < 0x04000000)
    {
      addCode(((code>>24)&0x03)+0xF8, buf, bufLen, ix);
      addCode(((code>>18)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
    else
    {
      addCode(((code>>30)&0x01)+0xFC, buf, bufLen, ix);
      addCode(((code>>24)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>18)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>12)&0x3F)+0x80, buf, bufLen, ix);
      addCode(((code>>06)&0x3F)+0x80, buf, bufLen, ix);
      addCode((code&0x3F)+0x80, buf, bufLen, ix);
    }
  }
  return ix;
}

static int utf8ToWideChar(const char *text, int tlen, wchar_t *buf, int bufLen)
{
  int len = tlen<0 ? strlen(text)+1 : tlen;
  int ix=0;
  for(int i=0; i<len; i++) 
  {
    unsigned int code = text[i];
    int charLefts = len - i;
    if (code < 128) {
      // standard ASCII
    } 
    else if ((code & 0xE0) == 0xC0 && charLefts >= 2) 
    {
      code = ((code & 0x1F) << 6) + (text[i + 1] & 0x3F);
      i += 1;
    } 
    else if ((code & 0xF0) == 0xE0 && charLefts >= 3) 
    {
      code = ((code & 0xF) << 12) +
        ((text[i + 1] & 0x3F) << 06) +
        ((text[i + 2] & 0x3F));
      i += 2;
    } 
    else if ((code & 0xF8) == 0xF0 && charLefts >= 4) 
    {
      code = ((code & 0x7) << 18) +
        ((text[i + 1] & 0x3F) << 12) +
        ((text[i + 2] & 0x3F) << 06) +
        ((text[i + 3] & 0x3F));
      i += 3;
    } 
    else if ((code & 0xFC) == 0xF8 && charLefts >= 5) 
    {
      code = ((code & 0x3) << 24) +
        ((text[i + 1] & 0x3F) << 18) +
        ((text[i + 2] & 0x3F) << 12) +
        ((text[i + 3] & 0x3F) << 06) +
        ((text[i + 4] & 0x3F));
      i += 4;
    } 
    else if ((code & 0xFC) == 0xF8 && charLefts >= 6) 
    {
      code = ((code & 0x1) << 30) +
        ((text[i + 1] & 0x3F) << 24) +
        ((text[i + 2] & 0x3F) << 18) +
        ((text[i + 3] & 0x3F) << 12) +
        ((text[i + 4] & 0x3F) << 06) +
        ((text[i + 5] & 0x3F));
      i += 5;
    }
    if (ix<bufLen) buf[ix] = code;
    ix++;
  }
  return ix;
}

int WideCharToMultiByte(
  unsigned int CodePage,        // code page
  long dwFlags,                 // performance and mapping flags
  const wchar_t *lpWideCharStr, // wide-character string
  int cchWideChar,              // number of chars in string
  char *lpMultiByteStr,         // buffer for new string
  int cbMultiByte,              // size of buffer
  const char *lpDefaultChar,    // default for unmappable chars
  bool *lpUsedDefaultChar      // set when default char used
)
{
  if (CodePage==CP_UTF8)
  {
    return WideCharToUTF8 (lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte);
  }
  else if (CodePage==CP_ACP)
  {
    int outsiz = wcstombs(lpMultiByteStr, lpWideCharStr, cbMultiByte);
    if (outsiz>0 && (outsiz<cbMultiByte || !lpMultiByteStr)) 
    {
      if (lpMultiByteStr) lpMultiByteStr[outsiz] = 0; //NULL terminate
      outsiz++; //count NULL terminate
    }
    return outsiz;
  }
  RptF("Character code page not defined: %d", CodePage);
  return 0;
}

int MultiByteToWideChar(
  unsigned int CodePage,        // code page
  long dwFlags,                 // character-type options
  const char *lpMultiByteStr,   // string to map
  int cbMultiByte,              // number of bytes in string
  wchar_t * lpWideCharStr,      // wide-character buffer
  int cchWideChar               // size of buffer
)
{
  if (CodePage==CP_UTF8)
  {
    return utf8ToWideChar(lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
  }
  else if (CodePage==CP_ACP)
  {
    int outsiz = mbstowcs(lpWideCharStr, lpMultiByteStr, cchWideChar);
    if (outsiz>0 && (outsiz<cchWideChar || !lpWideCharStr))
    {
      if (lpWideCharStr) lpWideCharStr[outsiz]=0; //NULL terminate
      outsiz++; //count NULL terminate
    }
    return outsiz;
  }
  RptF("Character code page not defined: %d", CodePage);
  return 0;
}

#endif

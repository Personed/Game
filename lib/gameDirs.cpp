#include "wpch.hpp"
#include "gameDirs.hpp"
#include <El/FileServer/fileServer.hpp>
#include <Es/Files/filenames.hpp>
#include "Es/Containers/staticArray.hpp"

/*!
\patch 5095 Date 12/1/2006 by Jirka
- Fixed: User profile ending with the period character was handled incorrectly
*/

RString EncodeFileName(RString src)
{
  const int len = FILENAME_MAX;
  char buffer[len];
  int dst = 0;
  for (const char *ptr=src; *ptr; ptr++)
  {
    if (dst >= len - 1)
    {
      Assert(dst == len - 1);
      buffer[dst] = 0;
      return buffer;
    }
    if
      (
      *ptr >= '0' && *ptr <= '9' ||
      *ptr >= 'A' && *ptr <= 'Z' ||
      *ptr >= 'a' && *ptr <= 'z'
      )
    {
      buffer[dst++] = *ptr;
    }
    else switch (*ptr)
    {
    case '^':
    case '&':
    case '\'':
    case '@':
    case '{':
    case '}':
    case '[':
    case ']':
    case ',':
    case '$':
    case '=':
    case '!':
    case '-':
    case '#':
    case '(':
    case ')':
    case '+':
    case '~':
    case '_':
      buffer[dst++] = *ptr;
      break;
    case '%':
      if (dst >= len - 2)
      {
        buffer[dst] = 0;
        return buffer;
      }
      buffer[dst++] = '%';
      buffer[dst++] = '%';
      break;
    default:
      if (dst >= len - 3)
      {
        buffer[dst] = 0;
        return buffer;
      }
      buffer[dst++] = '%';
      int hi = (int(*ptr) & 0xf0) >> 4;
      int lo = int(*ptr) & 0x0f;
      buffer[dst++] = hi > 9 ? 'a' + hi - 10 : '0' + hi;
      buffer[dst++] = lo > 9 ? 'a' + lo - 10 : '0' + lo;
      break;
    }
  }
  buffer[dst] = 0;
  return buffer;
}

RString DecodeFileName(RString src)
{
  char buffer[FILENAME_MAX];
  char *dst = buffer;
  for (const char *ptr=src; *ptr; ptr++)
  {
    if (*ptr == '%')
    {
      ptr++;
      if (*ptr == '%') *(dst++) = '%';
      else
      {
        char cHi = *(ptr++);
        char cLo = *ptr;
        int hi = cHi > '9' ? cHi - 'a' + 10 : cHi - '0';
        int lo = cLo > '9' ? cLo - 'a' + 10 : cLo - '0';
        *(dst++) = (hi << 4) | lo;
      }
    }
    else *(dst++) = *ptr;
  }
  *dst = 0;
  return buffer;
}

RString UnicodeToUTF(RWString text)
{
  // convert from Unicode to UTF-8
  int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  AUTO_STATIC_ARRAY(char, buffer, 1024);
  buffer.Resize(len);
  WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer.Data(), len, NULL, NULL);
  buffer[len - 1] = 0; // make sure result is always null terminated
  return buffer.Data();
}

RWString UTFToUnicode(RString text)
{
  // convert from UTF-8 to Unicode
  int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, text, -1, wBuffer.Data(), len);
  wBuffer[len - 1] = 0; // make sure result is always null terminated
  return wBuffer.Data();
}



#ifdef _WIN32
/// command line may be used to override user profile storage area 
/* does not terminate with backslash */
static RString ExplicitRootDir;

void SetExplicitRootDir(RString path)
{
  ExplicitRootDir = path;
  if (path.GetLength()>=0 && path[path.GetLength()-1]=='\\')
  {
    ExplicitRootDir = RString(path,path.GetLength()-1);
  }
  else
  {
    ExplicitRootDir = path;
  }
  CreatePath(path);
  CreateDirectory(path,NULL);
}
#endif

#if defined _WIN32

#include <Es/Common/win.h>

RString GetLoginName()
{
#if _ENABLE_DEDICATED_SERVER
  bool IsDedicatedServer();
  if (IsDedicatedServer()) return "Server";
#endif
  char buf[256];
  DWORD bufSize = sizeof(buf);
  if (::GetUserName(buf, &bufSize) && bufSize > 0) return buf;
  return "Player";
}

#include <shlobj.h>
#include <io.h>
#include <El/QStream/qStream.hpp>

/*!
\patch 5133 Date 2/26/2007 by Jirka
- Fixed: when alternate directory for profiles given by command line options -profiles,
user profiles are no longer stored to this directory, but to its Users subdirectory
*/

RStringB GetProfilePathDefault();
RStringB GetProfilePathCommon();

static RString GetRootDir()
{
  Assert(ExplicitRootDir.GetLength()==0);
  char path[MAX_PATH];
  if (FAILED(SHGetFolderPath(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, path)))
  {
    Fail("Cannot access My Documents");
    return RString();
  }
  TerminateBy(path, '\\');
  return path;
}

bool GetLocalSettingsDir(char *dir)
{
  if (ExplicitRootDir.GetLength()>0)
  {
    strcpy(dir,ExplicitRootDir);
    TerminateBy(dir, '\\');
    return true;
  }
  Assert(ExplicitRootDir.GetLength()==0);
  if (FAILED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, dir)))
  {
    Fail("Cannot access Local settings Application data");
    return false;
  }
  TerminateBy(dir, '\\');
  strcat(dir, AppName);
  return true;
}

bool GetLocalSettingsDir(wchar_t *dir)
{
  if (ExplicitRootDir.GetLength()>0)
  {
    MultiByteToWideChar(CP_ACP, 0, ExplicitRootDir, -1, dir, MAX_PATH);
    TerminateBy(dir, '\\');
    return true;
  }
  Assert(ExplicitRootDir.GetLength()==0);
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, dir)))
  {
    Fail("Cannot access Local settings Application data");
    return false;
  }
  TerminateBy(dir, '\\');

  
  wchar_t appNameW[256];
  mbstowcs(appNameW, AppName, sizeof(appNameW));

  wcscat(dir, appNameW);
  return true;
}

/// all whitspaces make it empty
bool IsEmptyPlayerName(RString name)
{
  RWString wideName = UTFToUnicode(name);
  for (const wchar_t *c=wideName; *c; c++)
  {
    if (*c>0x20) return false;
  }
  return true;
}

/// any control characters make it invalid
bool IsValidPlayerName(RString name)
{
  RWString wideName = UTFToUnicode(name);
  for (const wchar_t *c=wideName; *c; c++)
  {
    if (*c<0x20) return false;
  }
  return true;
}


bool ForEachUser(ForEachUserCallback func, void *context)
{
  RString dir;
  
  if (ExplicitRootDir.GetLength()>0)
  {
    dir = ExplicitRootDir + RString("\\Users");
  }
  else
  {
    RString root = GetRootDir();
    if (root.GetLength() == 0) return false;
    
    // check primary location
    {
      RString dir = GetProfilePathDefault();
      if (QIFileFunctions::DirectoryExists(root + dir))
      {
        if (func(GetLoginName(), context)) return true;
      }
    }

    dir = root + GetProfilePathCommon();
  }

  _finddata_t info;
  long h = _findfirst(dir + RString("\\*.*"), &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
      {
        RString userName = DecodeFileName(info.name);
        if (!IsEmptyPlayerName(userName) && IsValidPlayerName(userName))
        {
          if (func(userName, context)) return true;
        }
        else
        {
          RptF("Invalid player profile '%s' ignored.",cc_cast(userName));
        }
      }
    }
    while (_findnext(h, &info) == 0);
    _findclose(h);
  }
  return false;
}

RString GetUserRootDir(RString name)
{
  (void) name; //suppress warning
  if (ExplicitRootDir.GetLength()>0)
  {
    return ExplicitRootDir + RString("\\Users\\") + EncodeFileName(name);
  }

  RString root = GetRootDir();
  if (root.GetLength() == 0) return root;

#if _ENABLE_DEDICATED_SERVER
  bool IsDedicatedServer();
  if (IsDedicatedServer()) 
  { // for server use path like: C:\Users\xxx\Documents\DayZ Other Profiles\Server
    RString dir = GetProfilePathCommon();
    return root + dir + "\\" + GetLoginName();
  }
#endif

  RString dir = GetProfilePathDefault();
  return root + dir;
/*
    // NOTE: ProfilePathCommon is not currently used for other profiles, as there is only player and server profile in DayZ. 
    //       One can still switch to different by -profiles="path"
    RString dir = GetProfilePathCommon();
    return root + dir + "\\" + EncodeFileName(name);
*/
}

RString GetDefaultUserRootDir()
{
  if (ExplicitRootDir.GetLength()>0)
  {
    return ExplicitRootDir + RString("\\Users\\") + EncodeFileName(GetLoginName());
  }
  RString root = GetRootDir();
  if (root.GetLength() == 0) return root;

  RString dir = GetProfilePathDefault();
  return root + dir;
}

#else

RString GetUserRootDir()
{
  return RString(".");
}

bool GetLocalSettingsDir(char *dir)
{
  strcpy(dir,".");
  return false;
}

RString GetDefaultUserRootDir()
{
  return RString(".");
}

void SetExplicitRootDir(RString path)
{
}

RString GetUserRootDir(RString name)
{
  return name;
}

#endif


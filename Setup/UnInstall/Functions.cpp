#include <El/elementpch.hpp>

#include "windows.h"
#include "io.h"
#include "direct.h"
#include "stdlib.h"
#include "ctype.h"
#include <tchar.h>

#include <El/Stringtable/stringtable.hpp>
#include <El/WindowsFirewall/firewall.hpp>

#include "uninstall.h"

extern HINSTANCE hInst;
extern TCHAR applName[256];
extern TCHAR directory[_MAX_PATH];

/// Uninstalling of User data (local Settings, My Documents, etc.)
class UninstallFinal
{
public:
  // data from uninstall.log
  RString appName;
  RString productBinPath;
  AutoArray<RString> productBinEntry;
  RString poseidonPath;
  // data after initialization
  bool initialized;
  AutoArray<RString> directoryName; //value of entry from product.bin

  UninstallFinal() : initialized(false) {}

  void DoSetUninstallSpecialPathInfo(LPCTSTR sLine);
  /// initialize, trying use productBinPath + productBinEntry
  void Initialize();
  /// process uninstalling of final data (user data)
  void Process();
  /// local settings path
  RString GetUserLocalSettingsPath();
  /// my documents path
  AutoArray<RString> UninstallFinal::GetUserMyDocumentsDirs();
};

/* Special path info can be one of the following
A:appName
P:pathToProduct.bin
E:entryNameWithDirectoryInsideProduct.bin
*/
void UninstallFinal::DoSetUninstallSpecialPathInfo(LPCTSTR sLine)
{
  switch (sLine[0])
  {
  case _T('A'):
  case _T('a'):
    appName = LPCTSTR2RString(sLine+2);
    return;
    // break;
  case _T('P'):
  case _T('p'):
    productBinPath = LPCTSTR2RString(sLine+2);
    break;
  case _T('E'):
  case _T('e'):
    productBinEntry.Add(LPCTSTR2RString(sLine+2));
    break;
  case _T('M'):
  case _T('m'):
    poseidonPath = LPCTSTR2RString(sLine+2);
    break;
  }
  if (productBinEntry.Size()>0 && !productBinPath.IsEmpty() && !poseidonPath.IsEmpty())
  {
    Initialize();
  }
}

// global instance of UninstallFinal class
UninstallFinal gUninstFinal;
// global interface function for gameDirs
const AutoArray<RString> &GetProfilePathDirs()
{
  return gUninstFinal.directoryName;
}

// global interface function for gameDirs
RString GetAppName()
{
  return gUninstFinal.appName;
}

#define ARMA_UNINSTALL 1
extern void GetMainDirectoryFromRegistry(DWORD len, LPTSTR directory);

#include "gameDirs.hpp"
#include "Es/Files/filenames.hpp"
#include "El/ParamFile/paramFile.hpp"
void UninstallFinal::Initialize()
{
  if (productBinEntry.Size()>0 && !productBinPath.IsEmpty() && !poseidonPath.IsEmpty())
  {
    char productBinFullPath[1024];
    strcpy(productBinFullPath, poseidonPath);
    TerminateBy(productBinFullPath, '\\');
    strcat(productBinFullPath, productBinPath);
    bool found = true;
    if (_access(productBinFullPath,00)!=0)
    {
#if ARMA_UNINSTALL
      TCHAR curDir[MAX_PATH+1];
      curDir[0] = '\0';
      GetMainDirectoryFromRegistry(MAX_PATH, curDir);
  #if UNICODE      
      WideCharToMultiByte(CP_UTF8, 0, curDir, -1, productBinFullPath, 1024, NULL, NULL);
  #else
      strcpy (productBinFullPath, curDir);
  #endif
      TerminateBy(productBinFullPath, '\\');
      strcat(productBinFullPath, productBinPath);
      if (_access(productBinFullPath,00)!=0) 
#endif
        found = false;
    }
    if(found)
    { //file product.bin found
      ParamFile productBin;
      if (productBin.ParseBinOrTxt(productBinFullPath))
      {
        directoryName.Clear();
        for (int i=0; i<productBinEntry.Size(); i++)
        {
          directoryName.Add(productBin >> productBinEntry[i]); 
        }
        initialized = true;
      }
    }
  }
}

/// local settings path
RString UninstallFinal::GetUserLocalSettingsPath()
{
  char dir[1024];
  if (GetLocalSettingsDir(dir)) return RString(dir);
  else return RString("");
}

/// my documents path
AutoArray<RString> UninstallFinal::GetUserMyDocumentsDirs()
{
  return GetDefaultUserRootDirs();
}

#include <sys/stat.h>
static void DeleteDirectoryStructure(const char *name, bool deleteDir =true)
{
  if (!name || *name == 0) return;

  char buffer[256];
  sprintf(buffer, "%s\\*.*", name);

  _finddata_t info;
  long h = _findfirst(buffer, &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
        {
          sprintf(buffer, "%s\\%s", name, info.name);
          DeleteDirectoryStructure(buffer);
        }
      }
      else
      {
        sprintf(buffer, "%s\\%s", name, info.name);
        chmod(buffer,_S_IREAD | _S_IWRITE);
        unlink(buffer);
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
  if (deleteDir)
  {
    chmod(name,_S_IREAD | _S_IWRITE);
    rmdir(name);
  }
}

void UninstallFinal::Process()
{
  RString lcSetDir = GetUserLocalSettingsPath();
  if (!lcSetDir.IsEmpty() && _access(lcSetDir, 00)!=-1) 
    DeleteDirectoryStructure(lcSetDir);
  const AutoArray<RString> &myDocDir = GetUserMyDocumentsDirs();
  for (int i=0; i< myDocDir.Size(); i++)
  {
    if (_access(myDocDir[i], 00)!=-1) 
      DeleteDirectoryStructure(myDocDir[i]);
  }
}

/// global interface for main.cpp
void DoUninstallFinal()
{
  gUninstFinal.Process();
}

/// global interface for main.cpp
RString GetUserDirectoriesString()
{
  AutoArray<RString> dir1 = gUninstFinal.GetUserMyDocumentsDirs();
  RString dir2 = gUninstFinal.GetUserLocalSettingsPath();
  if (_access(dir2, 00)==-1) dir2="";
  bool isDir = !dir2.IsEmpty();
  RString out;
  if (isDir) out = Format("\n%s", cc_cast(dir2));
  for (int i=0; i<dir1.Size(); i++)
  {
    if (_access(dir1[i], 00)!=-1)
    {
      isDir = true;
      out = out + Format("\n%s", cc_cast(dir1[i]));
    }
  }
  if (isDir) out = out + "\n";
  return out;
}

///////////////////////////////////////////////////////////////////////////

/* DoCopyFile */
BOOL DoCopyFile(LPTSTR FileSrc,LPTSTR FileDst,BOOL bShowErr)
{
	// File Exist
	if (_taccess(FileSrc,00)!=0)
	{
		if (bShowErr)
		{
			TCHAR str[100];
			TCHAR str2[100+_MAX_PATH];
			TCHAR title[100];
			PosLoadString("UNINSTALL_FAIL_NOTFOUND",str,100);
			PosLoadString("UNINSTALL_APP_NAME",title,100);
			wsprintf(str2,str,FileSrc);
			MessageBox(NULL,str2,title,MB_OK|MB_ICONSTOP|MB_APPLMODAL);
		}
		return FALSE;
	}
	// copy file
	{
/*
    BOOL 	 bGood;

		OFSTRUCT ofStrSrc;
		OFSTRUCT ofStrDst;
		HFILE	 hSrc;
		HFILE	 hDst;
		
		bGood = TRUE;
		LZStart();
		hSrc = LZOpenFile(FileSrc,&ofStrSrc,OF_READ);				
		hDst = LZOpenFile(FileDst,&ofStrDst,OF_CREATE);				
		if ((hSrc==-1)||(hDst==-1))
		{
			bGood = FALSE;
		} else
		{
			LONG res;
			res = LZCopy(hSrc,hDst);
			if (res<0) bGood = FALSE;
		}
		LZClose(hSrc);
		LZClose(hDst);
		LZDone();
*/
    BOOL bGood = ::CopyFile(FileSrc, FileDst, FALSE);
		if (!bGood)
		{
			if (bShowErr)
			{
				TCHAR str[100];
				TCHAR str2[100+_MAX_PATH];
				TCHAR title[100];
				PosLoadString("UNINSTALL_CANNOT_COPYFILE",str,100);
				PosLoadString("UNINSTALL_APP_NAME",title,100);
				wsprintf(str2,str,FileDst);
				MessageBox(NULL,str2,title,MB_OK|MB_ICONSTOP|MB_APPLMODAL);
			};
			return FALSE;
		}
	}
	return TRUE;	
}

/* DoDeleteFile */
BOOL DoDeleteFile(LPCTSTR pFile)
{
	if (_taccess(pFile,00)==0)
	{                    
		/*
		OFSTRUCT ofs;
		OpenFile(pFile,&ofs,OF_DELETE);
		*/
		return DeleteFile (pFile);
	}

	return FALSE;
}

// check folder for empty, need to check before rmdir, because it delete non-empty softlink or junction 
bool isDirEmpty(LPCTSTR name)
{
  if (!name || *name == 0) return true;

  TCHAR buffer[256];
  _stprintf_s(buffer,  _T("%s\\*.*"), name);

  _tfinddata_t info;
  long h = _tfindfirst(buffer, &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        if (_tcscmp(info.name, _T(".")) != 0 && _tcscmp(info.name, _T("..")) != 0)
        {
          _findclose(h);
          return false;
        }
      }
      else
      {
        _findclose(h);
        return false;
      }
    }
    while (_tfindnext(h, &info)==0);
    _findclose(h);
  }
  return true;
}

/* DoDeleteDir */
void DoDeleteDir(LPCTSTR pDir)
{
	SetCurrentDirectory(_T("C:\\"));
  // delete only empty folder, rmdir doesn't delete empty folder but it delete non-empty softlink or junction 
	if (isDirEmpty(pDir)) _trmdir(pDir);
};

/* DoDeleteKey */
void DoDeleteKey(HKEY root, LPCTSTR buff)
{
	HKEY hKey;
	if (RegOpenKey(root,buff,&hKey)==ERROR_SUCCESS)
	{
		DWORD nSubKeys;
		if (RegQueryInfoKey(hKey,NULL,NULL,NULL,&nSubKeys,
										NULL,NULL,NULL,NULL,NULL,NULL,NULL)==ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			if (nSubKeys == 0)
				RegDeleteKey(root,buff);
		}
	};
};
/* DoDeleteValue */
void DoDeleteValue(HKEY root, LPCTSTR buff)
{
	HKEY hKey;
  int nRes;
  TCHAR str[260],path[260],name[260];

  lstrcpy(str,buff);
  nRes=-1;
  for(int i=0;i<lstrlen(str);i++)
    if (str[i]==_T(':'))
    {
      nRes=i+1;
      break;
    }
	if (nRes==-1)
  {
    DoDeleteKey(root, buff);
    return;
  }
	lstrcpy(name,str+nRes);
  lstrcpyn (path,str,nRes);



	if (RegOpenKey(HKEY_CURRENT_USER,path,&hKey)==ERROR_SUCCESS)
	{
		RegDeleteValue(hKey,name);
    RegCloseKey(hKey);
	};
};

typedef HRESULT (CALLBACK *RemoveFromGameExplorer)(GUID *);
typedef HRESULT (CALLBACK *RemoveTasks)(GUID *); 

void DoUnregisterFromGameExplorer(LPCTSTR tcharbuff)
{
  RString temp = LPCTSTR2RString(tcharbuff);
  LPCSTR buff = temp.MutableData();
  const char *path = strchr(buff, ':');
  if (!path) return;
  char strGuid[256];
  strncpy(strGuid, buff, path - buff);
  path++;

  HINSTANCE dll = LoadLibraryA(path);
  if (dll == NULL) return;

  GUID guid;
  sscanf(strGuid, "{%8X-%4X-%4X-%2X%2X-%2X%2X%2X%2X%2X%2X}",
    &guid.Data1, &guid.Data2, &guid.Data3,
    &(guid.Data4[0]), &(guid.Data4[1]), &(guid.Data4[2]), &(guid.Data4[3]),
    &(guid.Data4[4]), &(guid.Data4[5]), &(guid.Data4[6]), &(guid.Data4[7]));

  RemoveFromGameExplorer removeFromGameExplorer = (RemoveFromGameExplorer)GetProcAddress(dll, "RemoveFromGameExplorer");
  if (removeFromGameExplorer)
  {
    removeFromGameExplorer(&guid);
  }
  RemoveTasks removeTasks = (RemoveTasks)GetProcAddress(dll, "RemoveTasks");
  if (removeTasks)
  {
    removeTasks(&guid);
  }

  FreeLibrary(dll);       
}

void DoUnregisterFromWindowsFirewall(LPCTSTR path)
{
  WindowsFirewallRemoveApplication(path);
}

#if UNICODE
# define RTString RWString
#else
# define RTString RString
#endif

void DoDeleteFileTypeEx(RTString cmd)
{
  // parse the command
  LPCTSTR ptr = _tcschr(cmd, ':');
  if (!ptr) return;
  RTString extension(cmd, ptr - (LPCTSTR)(cmd));
  RTString productName(ptr + 1);

  // remove the item from the applications table
  ::RegDeleteKey(HKEY_CLASSES_ROOT, extension + _T("\\AppsTable\\") + productName);

  // check the current association
  HKEY key;
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, extension, 0, KEY_READ, &key) != ERROR_SUCCESS) return; // association corrupted
  TCHAR buffer[1024];
  DWORD size = sizeof(buffer);
  DWORD type = REG_SZ;
  LONG result = ::RegQueryValueEx(key, NULL, 0, &type, (BYTE *)buffer, &size);  
  ::RegCloseKey(key);
  if (result != ERROR_SUCCESS)
  {
    // association corrupted, remove it
    ::RegDeleteKey(HKEY_CLASSES_ROOT, extension);
    return;
  }
  RTString fileTypeName = buffer;

  // check the application table, select the best application to associate to
  RTString bestProduct;
  int maxFileVersion = -1;
  FILETIME maxWriteTime = {0, 0};
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, extension + _T("\\AppsTable"), 0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    for (int i=0;;i++)
    {
      // subkeys enumeration
      size = sizeof(buffer);
      FILETIME writeTime;
      if (::RegEnumKeyEx(key, i, buffer, &size, NULL, NULL, NULL, &writeTime) != ERROR_SUCCESS) break;
      // buffer contains product name now

      // open the key and read the file version
      HKEY subkey;
      if (::RegOpenKeyEx(key, buffer, 0, KEY_READ, &subkey) != ERROR_SUCCESS) continue; // check only valid items
      int fileVersion;
      size = sizeof(fileVersion);
      type = REG_DWORD;
      LONG result = ::RegQueryValueEx(subkey, _T("fileVersion"), 0, &type, (BYTE *)&fileVersion, &size);  
      ::RegCloseKey(subkey);
      if (result != ERROR_SUCCESS) continue; // check only valid items

      if (fileVersion < maxFileVersion) continue; // worse file version
      if (fileVersion == maxFileVersion && writeTime.dwHighDateTime < maxWriteTime.dwHighDateTime) continue; // worse write time
      if (fileVersion == maxFileVersion && writeTime.dwHighDateTime == maxWriteTime.dwHighDateTime && writeTime.dwLowDateTime < maxWriteTime.dwLowDateTime) continue; // worse write time

      // new best product found
      bestProduct = buffer;
      maxFileVersion = fileVersion;
      maxWriteTime = writeTime;
    }
    ::RegCloseKey(key);
  }

  if (bestProduct.GetLength() == 0)
  {
    // no valid product in applications table, remove the file association at all
    ::RegDeleteKey(HKEY_CLASSES_ROOT, extension + _T("\\AppsTable"));
    ::RegDeleteKey(HKEY_CLASSES_ROOT, extension);
    ::RegDeleteKey(HKEY_CLASSES_ROOT, fileTypeName + _T("\\shell\\open\\command"));
    ::RegDeleteKey(HKEY_CLASSES_ROOT, fileTypeName + _T("\\shell\\open"));
    ::RegDeleteKey(HKEY_CLASSES_ROOT, fileTypeName + _T("\\shell"));
    ::RegDeleteKey(HKEY_CLASSES_ROOT, fileTypeName + _T("\\DefaultIcon"));
    ::RegDeleteKey(HKEY_CLASSES_ROOT, fileTypeName);
    return;
  }

  // copy the values from applications table to the association description
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, extension + _T("\\AppsTable\\") + bestProduct, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
  // read exe path
  size = sizeof(buffer);
  type = REG_SZ;
  if (::RegQueryValueEx(key, NULL, 0, &type, (BYTE *)buffer, &size) != ERROR_SUCCESS)
  {
    ::RegCloseKey(key);
    return;
  }
  RTString exePath = buffer;
  // read default icon
  size = sizeof(buffer);
  type = REG_SZ;
  if (::RegQueryValueEx(key, _T("defaultIcon"), 0, &type, (BYTE *)buffer, &size) != ERROR_SUCCESS)
  {
    ::RegCloseKey(key);
    return;
  }
  RTString defaultIcon = buffer;
  ::RegCloseKey(key);
  // write file version
  if (::RegCreateKey(HKEY_CLASSES_ROOT, extension, &key) == ERROR_SUCCESS)
  {
    ::RegSetValueEx(key, _T("fileVersion"), 0, REG_DWORD, (BYTE *)&maxFileVersion, sizeof(int));
    ::RegCloseKey(key);
  }
  // write the command
  RTString regKeyCommand = fileTypeName + _T("\\shell\\open\\command");
  if (::RegCreateKey(HKEY_CLASSES_ROOT, regKeyCommand, &key) == ERROR_SUCCESS)
  {
    RTString command = _T("\"") + exePath + _T("\" \"%1\"");
    ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)command, (command.GetLength() + 1) * sizeof(TCHAR));
    ::RegCloseKey(key);
  }
  // write the default icon
  RTString regKeyDefaultIcon = fileTypeName + _T("\\DefaultIcon");
  if (::RegCreateKey(HKEY_CLASSES_ROOT, regKeyDefaultIcon, &key) == ERROR_SUCCESS)
  {
    ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)defaultIcon, (defaultIcon.GetLength() + 1) * sizeof(TCHAR));
    ::RegCloseKey(key);
  }
}

bool FileExists(LPCTSTR path)
{
  FILE *test = _tfopen(path,_T("rb"));
  if (test)
  {
    fclose(test);
    return true;
  }
  return false;
}

// for "arma.exe" if this file exist in "dir" return true otherwise false
// for "!arma.exe" return true if file doesn't exist
bool TestCondition(LPCTSTR pCmd,LPCTSTR dir)
{
  if (pCmd && pCmd[0]!=_T('\0'))
  {
    bool negate = pCmd[0]=='!'; 
    TCHAR buff[1000];
    lstrcpy(buff,dir);
    lstrcat(buff,_T("\\"));

    if (negate) 
    {
      lstrcat(buff,pCmd+1);
      if (FileExists(buff)) return false; 
    }
    else 
    {
      lstrcat(buff,pCmd);
      if (!FileExists(buff))  return false; 
    }
  }
  return true;
}

/* DoUnInstallCmd */
void DoUnInstallCmd(LPCTSTR pCmd)
{
	TCHAR buff[1000];

	if (lstrlen(pCmd)<3)
		return;
	if (pCmd[1] != _T(':'))
		return;

	lstrcpy(buff,pCmd+2);
	switch(pCmd[0])
	{
  // remove file if condition is true
  case _T('E'):
  case _T('e'):	// delete file
  {
    //lstrcpy(sfExe, directory);
    TCHAR *name = _tcschr(buff,':');
    if (name!=0)
    {
      name++; // skip ':'
      TCHAR condition[1000];
      lstrcpyn(condition,buff,name-buff);

      if (TestCondition(condition,directory))
        DoDeleteFile(name);
    };
    break;
  }
	case _T('F'):
	case _T('f'):	// delete file
		DoDeleteFile(buff);
		break;
	case _T('D'):
	case _T('d'):	// delete dir
		DoDeleteDir(buff);
		break;
	case _T('K'):
	case _T('k'):	// delete key LOCAL_MACHINE
		DoDeleteValue(HKEY_LOCAL_MACHINE, buff);
		break;
  case _T('C'):
  case _T('c'):	// delete key CLASSES_ROOT
    DoDeleteValue(HKEY_CLASSES_ROOT, buff);
    break;
  case _T('U'):
  case _T('u'):	// delete key HKEY_CURRENT_USER
    DoDeleteValue(HKEY_CURRENT_USER, buff);
    break;
  case _T('G'):
  case _T('g'):	// unregister from the Game Explorer
    DoUnregisterFromGameExplorer(buff);
    break;
  case _T('W'):
  case _T('w'):	// unregister from the Windows Firewall
    DoUnregisterFromWindowsFirewall(buff);
    break;
  case _T('P'):
  case _T('p'):
    gUninstFinal.DoSetUninstallSpecialPathInfo(buff);
    break;
  case _T('Q'):
  case _T('q'):
    UninstallProtect();
    break;
  case _T('A'):
  case _T('a'):	// unregister extended file association
    DoDeleteFileTypeEx(buff);
    break;
  case _T(';'):// comment
    break;
	};
};
    // uninstall StarForce protection
void UninstallProtect()
{
    TCHAR sfExe[_MAX_PATH];
    lstrcpy(sfExe, directory);
    lstrcat(sfExe, _T("\\protect.exe"));
    if (_taccess(sfExe, 00) == 0)
    {
      TCHAR sfCommand[_MAX_PATH + 256];
      lstrcpy(sfCommand, _T("\""));
      lstrcat(sfCommand, sfExe);
      lstrcat(sfCommand, _T("\" /drv:remove /nogui"));
      
      STARTUPINFO stInfo;
      memset(&stInfo, 0, sizeof(stInfo));
      stInfo.cb = sizeof(stInfo);

      PROCESS_INFORMATION prInfo;
      memset(&prInfo, 0, sizeof(prInfo));

      if (::CreateProcess(NULL, sfCommand, NULL, NULL,
        FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
        NULL, NULL, &stInfo, &prInfo))
      {
        ::WaitForSingleObject(prInfo.hProcess, INFINITE);

        if (prInfo.hThread) ::CloseHandle(prInfo.hThread);
        if (prInfo.hProcess) ::CloseHandle(prInfo.hProcess);
      }
    }
};

/* PosLoadString */
void PosLoadString (const char *uID, LPTSTR lpBuffer, int nBufferMax)
{
	static char buf[1024];
  static char tbuf[1024];
	char *lastSrc;
	char *sub;
	int c;

	strcpy (buf, LocalizeString (uID));

	lastSrc = &buf[0];
	tbuf[0] = '\0';

	sub = strstr (buf, "%0");
	c = 0;
	while (sub != NULL)
	{
		strncat (tbuf, lastSrc, sub - lastSrc);
		strcat (tbuf, LPCTSTR2RString(applName).Data());
		lastSrc = sub + 2;
		sub = strstr (lastSrc, "%0");
		++c;
	}
	strcat (tbuf, lastSrc);
#if UNICODE
  MultiByteToWideChar(CP_UTF8, 0, tbuf, -1, lpBuffer, nBufferMax);
#else
  strcpy (lpBuffer, tbuf);
#endif
}

RString LPCTSTR2RString(LPCTSTR str, int codePage/*=CP_ACP*/)
{
#ifdef UNICODE
  int size = WideCharToMultiByte(codePage, 0, str, -1, NULL, 0, NULL, NULL);
  RString buf;
  buf.CreateBuffer(size);
  WideCharToMultiByte(codePage, 0, str, -1, buf.MutableData(), size, NULL, NULL);
  return buf;
#else
  return RString(str);
#endif
}

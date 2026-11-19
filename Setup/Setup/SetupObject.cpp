#include "stdafx.h"
#include <El/elementpch.hpp>
#include <io.h>
//#include "dsetup.h"
#include "SetupObject.h"
#include "Setup.h"
#include "InsertCdRomDlg.h"
#include "el/Encryption/pboheaderprotection.h"
#include <El/Stringtable/stringtable.hpp>
#include <El/QStream/qStream.hpp>
#include <El/QStream/qBStream.hpp>  
#include <El/dlc/DLCTools.h>  
#include <direct.h>

#define REINFORCEMENTS 0

#pragma comment(lib, "Version.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

BOOL GetParamsSubstrings(LPCTSTR pLine,CString& sSource,CString& sDest)
{
  CString sLine = pLine;
  int nLn = sLine.GetLength();
  int nDv = sLine.Find(_T (':'));
  if (nDv < 0) return FALSE;

  if (nDv > 0)
    sSource = sLine.Left(nDv);
  else
    sSource.Empty();

  if (nDv < (nLn-1))
    sDest = sLine.Right(nLn-(nDv+1));
  else
    sDest.Empty();
  return TRUE;
};

bool SetupRegistryKey::Init(CString pLine) 
{
  // Q:registry:ValueName:ValueType:RegistryPathRoot:PathToRegistryKey:ValueData
  // line contain only the following now: 
  //    ValueName:ValueType:RegistryPathRoot:PathToRegistryKey:ValueData
  CString value, rest, line;
  // valueName
  line = pLine;
  if (!GetParamsSubstrings(line, value, rest)) return false;
  valueName = value.GetBuffer();
  // valueType
  line = rest;
  if (!GetParamsSubstrings(line, value, rest)) return false;
  if (value.CompareNoCase(_T("string"))==0) valueType = REG_SZ;
  else return false; //unknown data type (or not implemented yet)
  // root
  line = rest;
  if (!GetParamsSubstrings(line, value, rest)) return false;
  if (value.CompareNoCase(_T("hklm"))==0) root = HKEY_LOCAL_MACHINE;
  else if (value.CompareNoCase(_T("hkcu"))==0) root = HKEY_CURRENT_USER;
  else if (value.CompareNoCase(_T("hkcr"))==0) root = HKEY_CLASSES_ROOT;
  else return false; //unknown root (or not implemented yet)
  // path
  line = rest;
  if (!GetParamsSubstrings(line, value, rest)) return false;
  path = value;
  // valueData
  valueData = rest;
  return true;
}

CString GetTempDirectory(CString appNamePar)
{
  CString appName = appNamePar;
  appName.Replace(_T(':'), _T(' ')); //there cannot be colons inside directory or file name
  TCHAR buff[1];
  DWORD size=GetTempPath(1,buff);
  if (size==0) return _T("");
  size++;
  Temp<TCHAR> out; out.Realloc(size);
  //RString out; out.CreateBuffer(size);
  if (GetTempPath(size,out)==0) return CString(_T(""));
  return (CString(out) + appName + CString(_T(" - Installer")));
}

class FileBufferFromMemory: public IFileBuffer
{
protected:
  const char *_data;
  int size;

public:
  FileBufferFromMemory(const char *dt, int sz) : _data(dt), size(sz) {}

  const char *GetData() const {return _data;}
  // non-virtual writable access
  //char *GetWritableData() {return _data;}


  bool PreloadBuffer(
    FileRequestCompletionHandle &request, RequestableObject *obj, RequestableObject::RequestContext *ctx,
    FileRequestPriority priority, QFileSize pos, QFileSize size
    ) const
  {
    // data are always ready
    return true;
  }

  QFileSize GetSize() const {return size;}
  bool GetError() const {return false;}
  bool IsFromBank(QFBank *bank) const {return false;}
  bool IsReady() const {return true;}
};

//! create path to given directory
static void CreatePath(char *path)
{
  // string will be changed temporary
  char *end = path;
  while (end = strchr(end, '\\'))
  {
    *end = 0;
    mkdir(path);
    *end = '\\';
    end++;
  }
}

#define RES_TYPE MAKEINTRESOURCE(RT_RCDATA)

static HMODULE hresF = 0;
void *FindAndLoadResource(int &size, const void *ctx, const char *source)
{
  // ctx is here not only to make function signature similiar to FindAndLoadResource
  // but also this is pointer to CSetupObject now
  // in PARSE_RESOURCE_OURSELVES branch
#ifdef UNICODE
  Temp<TCHAR> upName;
  {
    int wSize = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    upName.Realloc(wSize);
    MultiByteToWideChar(CP_UTF8, 0, source, -1, upName.Data(), wSize);
  }
#else
  char upName[512];
  strcpy(upName,source);
#endif
  _tcsupr(upName);
  CSetupObject *setupObject = (CSetupObject *)ctx;
  if (setupObject && setupObject->m_patchingSetup.IsEmpty())
  {
    HRSRC hRsc = FindResource(NULL, upName, RES_TYPE);
    if (!hRsc)
    {
      return NULL;
    }
    HGLOBAL hMem = LoadResource(NULL, hRsc);
    size = SizeofResource(NULL, hRsc);
    return LockResource(hMem);
  }
  else
  {
    if (!hresF)
    {
      hresF = LoadLibrary(setupObject->m_patchingSetup);
    }
    if (!hresF) return NULL;
    HRSRC hRsc = FindResource(hresF, upName, RES_TYPE);
    if (!hRsc)
    {
      return NULL;
    }
    HGLOBAL hMem = LoadResource(hresF, hRsc);
    size = SizeofResource(hresF, hRsc);
    return LockResource(hMem);
  }
}

static RString GetFileContent(CString fileName)
{
  Temp<char> tempbuf;
  int size = 0;
  CFile  file;
  size = QIFileFunctions::GetFileSize(LPCTSTR2RString(fileName));
  file.Open(fileName,CFile::modeRead);
  tempbuf.Realloc(size+1);
  file.Read(tempbuf,size);
  tempbuf[size] = 0;
  file.Close();
  return RString(tempbuf);
}

int SkipUnicodePrefix(const char * textData)
{
  int len = strlen(textData);
  if (len<3) return 0;
  unsigned char c1 = textData[0];
  unsigned char c2 = textData[1];

  if (c1 == 0xff && c2 == 0xfe) return 2;
  if (c1 == 0xfe && c2 == 0xff) return 2;
  if (c1 == 0xef && c2 == 0xbb)
  {
    if (len>=3 && (unsigned char)textData[2]==0xbf) return 3; // UTF-8 prefix
  }
  return 0;
}

static RString StrWithoutUnicodePrefix(const char *data)
{
  if (!data) return RString("");
  int ix = SkipUnicodePrefix(data);
  return RString(data+ix);
}

RString CSetupObject::LoadFile(CString fileName, BOOL reportErrors)
{
  //we will try to read license data from setup.exe resources
  int size;
  char *loadedData = (char *)FindAndLoadResource(size, this, LPCTSTR2RString(fileName));
  if (loadedData) return StrWithoutUnicodePrefix(loadedData);
  else //data are not in resources, so these can be located on temporary directory
  {
    if (!temporaryFolderActive) PrepareResourceData();
    CString tempDir = GetTempDirectory(m_sApplicationName);
    CString fileTempName = tempDir + CString(_T("\\")) + fileName;
    extern bool FileExists(CString &path);
    if (FileExists(fileTempName))
    {
      return StrWithoutUnicodePrefix(GetFileContent(fileTempName));
    }
    else 
    {
      if (!VerifyFileRead(fileName,reportErrors)) return RString("");
      return StrWithoutUnicodePrefix(GetFileContent(fileName));
    }
  }
}

inline LPWSTR GetFilenameExt( LPWSTR w )
{	// short name with extension
  LPWSTR nam = wcsrchr(w,_T('\\'));
  if( nam ) return nam+1;
  if( w[0]!=0 && w[1]==':' ) return w+2;
  return w;
}

//Save the specified file from bank
void SaveFile(const FileInfoO &fi, const FileBankType *files, void *context)
{
  SaveContext *sc = (SaveContext *)context;
  // save given file
  QIFStreamB file;
  file.open(*sc->bank,fi.name);
  // note: file may be compressed - auto handled by bank

  RString outFilename;
  if (sc->outfile.IsEmpty())
    outFilename = sc->folder+RString("\\")+fi.name;
  else
    outFilename = sc->outfile;
  // note file name may contain subfolder
  char folderName[1024];
  strcpy(folderName,outFilename);
  *GetFilenameExt(folderName) = 0;
  if (strlen(folderName)>0)
  {
    if (folderName[strlen(folderName)-1]='\\') folderName[strlen(folderName)-1]=0;
    // FIX: create whole path (not only topmost directory)
    CreatePath(folderName);
    mkdir(folderName);
  }
  // 
  //	FILE *tgt = fopen(outFilename,"wb");
  //	if (!tgt) return;

  //	setvbuf(tgt,NULL,_IOFBF,256*1024);
  QOFStream out(outFilename);
  file.copy(out);
  out.close();
  //  fwrite(file.act(),file.rest(),1,tgt);

  //	fclose(tgt);
}

/////////////////////////////////////////////////////////////////////////////
// CSetupObject - ridici objekt instalace


CString CSetupObject::m_sApplicationName ("Application");
CString CSetupObject::m_sFileLanguageExtension;


bool CSetupObject::m_testVolumeLabel = true;
CSetupObject::CSetupObject()
	: m_sSetupPoseidonDir ("C:\\Program Files\\Bohemia Interactive"),
	m_sInstallationSubdirectory ("Bohemia Interactive"),
	m_sStartMenuGroupDir ("Bohemia Interactive"),
	m_sRegistryRootDefault ("Software\\Bohemia Interactive"),
  m_sCheckFileName("checkFileList.lst"),
  m_sRunAfterUpdate(""),
  m_sTargetFileCheck (""),
  m_bTargetFileRequired (false),
	m_pPosSetupApp (NULL), m_currentCdRom (NULL), m_TotalCopySize (0),
  m_processDirectX(DRX_DIALOG),
  m_SetupFileListLoaded(false),
  m_installOpenAL(true),
  m_saveVersionFile(false),
  m_processLicense(false),
  m_processInfo(false),
  m_allUsers(true),
  m_licenceButtonDisabled(false),
  m_CDKeyButtonDisabled(true),
  m_debugMode(false),
  m_showSetupMode(false),
  m_updaterMode(false),
  m_runAfterUpdateFlag(false),
  m_licencePageIndex(-1), //no page can be disabled by default
  m_CDKeyPageIndex(-1),
  m_iParentProjectId(-1), //set default to OA
  m_waitOnRunningProcess(false)
{
  temporaryFolderActive = false;
  m_nCurrState	= SETUP_STATE_BEGIN;
	m_nSetupResult	= stpOK;

  m_bAllUsers = FALSE;

	// Direct X
	m_dwVersion	=
	m_dwRevision = 0;
	// inicializace kritických sekcí
  m_bRunningRUNAPP = FALSE;
	_copyThread=NULL;
  _RunAppThread=NULL;
  m_showWelcome=true;
  m_forceInstall=FI_NO;
  m_showResultOK=true;
  m_doNotAskToDestFolder=false;
	InitializeCriticalSection(&m_CSection_CopyFiles);
  InitializeCriticalSection(&m_CSection_RUNAPP);
};

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

CSetupObject::~CSetupObject()
{	
	// ukoncení procesu 
	CancelCopy();
  CancelRunApp();

	// likvidace seznamu souborù
	for (int i=0; i<m_arFileList.GetSize(); i++)
	{
		CSetupFileItem* pItem = (CSetupFileItem*)(m_arFileList[i]);
		if (pItem)
			delete pItem;
	}
	m_arFileList.RemoveAll();

	for (int i=0; i<m_arShortcuts.GetSize(); i++)
	{
		CSetupFileItem* pItem = (CSetupFileItem*)(m_arShortcuts[i]);
		if (pItem)
			delete pItem;
	}
	m_arShortcuts.RemoveAll();

  for (int i=0; i<m_arShortcutsDesktop.GetSize(); i++)
  {
    CSetupFileItem* pItem = (CSetupFileItem*)(m_arShortcutsDesktop[i]);
    if (pItem)
      delete pItem;
  }
  m_arShortcutsDesktop.RemoveAll();

  for (int i=0; i<m_arShortcutsSendTo.GetSize(); i++)
  {
    CSetupFileItem* pItem = (CSetupFileItem*)(m_arShortcutsSendTo[i]);
    if (pItem)
      delete pItem;
  }
  m_arShortcutsSendTo.RemoveAll();

	for (int i=0; i<m_arPictures.GetSize(); i++)
	{
		CPictureItem *pItem = (CPictureItem *)(m_arPictures[i]);
		if (pItem) delete pItem;
	}
	m_arPictures.RemoveAll ();

	for (int i = 0; i < m_arActions.GetSize (); ++i)
	{
		CString *action = (CString *) (m_arActions[i]);
		if (NULL != action)
		{
			delete action;
		}
	}
	m_arActions.RemoveAll ();

	for (int i = 0; i < m_arSetupLogFiles.GetSize (); ++i)
	{
		CSetupLogFileItem *uninstall = (CSetupLogFileItem *) (m_arSetupLogFiles[i]);
		if (NULL != uninstall)
		{
			delete uninstall;
		}
	}
	m_arSetupLogFiles.RemoveAll ();

	for (int i = 0; i < m_cds.GetSize (); ++i)
	{
		CCdRom *cd = m_cds[i];
		if (cd != NULL)
		{
			delete cd;
		}
	}
	m_cds.RemoveAll ();

	for (int i = 0; i < m_arCrcFiles.GetSize (); ++i)
	{
		CCrcFile *crcFile = m_arCrcFiles[i];
		if (crcFile != NULL)
		{
			delete crcFile;
		}
	}
	m_arCrcFiles.RemoveAll ();

  m_delFilesAfterInstallEnd.RemoveAll();
  m_delDirsAfterInstallEnd.RemoveAll();
  m_updateUnistallAfterInstallEnd.RemoveAll();
};


/// Check access rights to "Program Files" directory and "HKEY_LOCAL_MACHINE/SOFTWARE" registry
static bool CheckAccessRights()
{
#if 0
  srand ( (unsigned)time( NULL ));
  extern bool GIsRunas;
  TCHAR msgBuf[256]; _stprintf(msgBuf, _T("Random number: %d, runas=%d"), rand(), GIsRunas);
  MessageBox(NULL, msgBuf, _T("Setup Module Handle Diagnostics"), 0);
#endif

  // perform security impersonation of the user and open the resulting thread token.
  HANDLE token;
  if (!ImpersonateSelf(SecurityImpersonation))
    return false;
  if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_READ, FALSE, &token))
    return false;
  RevertToSelf();

  // check "Program Files" directory
  TCHAR path[MAX_PATH];
  if (FAILED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES | CSIDL_FLAG_CREATE, NULL, 0, path)))
  {
    CloseHandle(token);
    return false;
  }
  // check the size
  DWORD size = 0;
  GetFileSecurity(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, 0, &size);
  if (size == 0)
  {
    CloseHandle(token);
    return false;
  }
  // retrieve the descriptor
  PSECURITY_DESCRIPTOR descriptor = (PSECURITY_DESCRIPTOR)(new char[size]);
  if (descriptor == NULL)
  {
    CloseHandle(token);
    return false;
  }
  if (!GetFileSecurity(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, descriptor, size, &size))
  {
    delete [] (char *)descriptor;
    CloseHandle(token);
    return false;
  }
  // perform access check using the token
  PRIVILEGE_SET privilegeSet;
  DWORD privilegeSetSize = sizeof(PRIVILEGE_SET);
  DWORD desiredAccess;
  GENERIC_MAPPING genericMapping;
  DWORD grantedAccess;
  BOOL accessGranted;
  memset(&genericMapping, 0, sizeof (GENERIC_MAPPING));
  desiredAccess = FILE_GENERIC_WRITE;
  genericMapping.GenericWrite = GENERIC_WRITE;
  MapGenericMask(&desiredAccess, &genericMapping);
  HRESULT result = AccessCheck(descriptor, token, desiredAccess, &genericMapping, &privilegeSet, &privilegeSetSize, &grantedAccess, &accessGranted);
  if (FAILED(result))
  {
    delete [] (char *)descriptor;
    CloseHandle(token);
    return false;
  }
  delete [] (char *)descriptor;
  descriptor = NULL;
  if (!accessGranted)
  {
    CloseHandle(token);
    return false;
  }

  // check "HKEY_LOCAL_MACHINE/SOFTWARE" registry
  HKEY key;
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE"), 0, KEY_READ, &key) != ERROR_SUCCESS)
  {
    CloseHandle(token);
    return false;
  }
  // check the size
  size = 0;
  RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, &size);
  if (size == 0)
  {
    CloseHandle(token);
    return false;
  }
  // retrieve the descriptor
  descriptor = (PSECURITY_DESCRIPTOR)(new char[size]);
  if (descriptor == NULL)
  {
    CloseHandle(token);
    return false;
  }
  if (RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, descriptor, &size) != ERROR_SUCCESS)
  {
    CloseHandle(token);
    return false;
  }
  // perform access check using the token
  memset(&genericMapping, 0, sizeof (GENERIC_MAPPING));
  desiredAccess = KEY_WRITE;
  genericMapping.GenericWrite = GENERIC_WRITE;
  MapGenericMask(&desiredAccess, &genericMapping);
  result = AccessCheck(descriptor, token, desiredAccess, &genericMapping, &privilegeSet, &privilegeSetSize, &grantedAccess, &accessGranted);
  delete [] (char *)descriptor;
  CloseHandle(token);
  return SUCCEEDED(result) && accessGranted;
}

/////////////////////////////////////////////////////////////////////////////
// jednotlivé kroky instalace

void CSetupObject::PrepareResourceData(const char *resourceName)
{
  //unpack SETUP_3RD_PARTY_AND_DATA bank into temporary directory
  CString tmpDir = GetTempDirectory(m_sApplicationName);
  int size;
  char *resData = (char *)FindAndLoadResource(size, this, resourceName);
  if (!resData) return;
  //unpack data from resource into temporary folder
  QFBank testBank;
  Ref<FileBufferFromMemory> fileBuf = new FileBufferFromMemory(resData, size);
  SaveContext ctx;
  ctx.bank = &testBank;
  ctx.folder = CString2RString(tmpDir);
  testBank.open(fileBuf);
  testBank.ForEach(SaveFile,&ctx);
  testBank.close();
}

void CSetupObject::PrepareResourceData()
{
  if (!temporaryFolderActive)
  {
    PrepareResourceData("SETUP_3RD_PARTY_AND_DATA");
    //mark that temporary folder was created
    temporaryFolderActive = true;
  }
}

void CSetupObject::PrepareResourceDirectX()
{
  PrepareResourceData("DIRECTX_INSTALLER");
}

BOOL CSetupObject::DoSetupStep()
{
	m_nCurrState = DoSetupStep(m_nCurrState);
	return (m_nCurrState != SETUP_STATE_ENDAPP);
};


// for "arma.exe" if this file exist in m_sSetupPoseidonDir return true otherwise false
// for "!arma.exe" return true if file doesn't exist
bool CSetupObject::TestCondition(CString sCondition,CString sDirectory)
{
  if (!sCondition.IsEmpty())
  {
    bool negate = sCondition[0]=='!'; 
    CString fileToTest;

    extern bool FileExists(CString &path);

    CString sRunFile; 
    if (!sDirectory.IsEmpty())
      MakeLongDirStr(sRunFile,sDirectory);
    else
      MakeLongDirStr(sRunFile,m_sSetupPoseidonDir);

    if (negate) 
    {
      fileToTest = sCondition.Mid(1);
      if (FileExists(sRunFile+fileToTest)) return false; 
    }
    else 
    {
      fileToTest = sCondition;
      if (!FileExists(sRunFile+fileToTest))  return false; 
    }
  }
  return true;
}


typedef CString langDef[8]; 
static const langDef g_setupLanguagesFull = {
  _T(".french"),
  _T(".spanish"),
  _T(".italian"),
  _T(".german"),
  _T(".polish"),
  _T(".czech"),
  _T(".russian"),
  _T(".hungarian"),
};
static const langDef g_setupLanguagesShort = {
  _T(".lng_fr"),
  _T(".lng_sp"),
  _T(".lng_it"),
  _T(".lng_ge"),
  _T(".lng_pl"),
  _T(".lng_cz"),
  _T(".lng_ru"),
  _T(".lng_hu"),
};

const langDef *g_setupLanguages = &g_setupLanguagesFull;

bool GIsRunas = false;
void SetupIsRunas()
{
  GIsRunas = true;
}

static CString GetRegistryValue(HKEY hKey, CString valueName)
{
  const int MAX_VALUE_NAME = 1024;
  DWORD valLen = MAX_VALUE_NAME;
  TCHAR  val[MAX_VALUE_NAME];
  DWORD type;
  HRESULT result = ::RegQueryValueEx(hKey, valueName, 0, &type, (LPBYTE)val,&valLen);
  if ( result==ERROR_SUCCESS && type==REG_SZ )
  {
    return CString(val);
  }
  return CString();
}

int	 CSetupObject::DoSetupStep(int nState)
{
	if (nState >= SETUP_STATE_USER0)
	{	// provedení uživatelského kroku
		return DoUserSetupStep(nState);
	};
	// realizace standardních krokù
	switch(nState)
	{
	case SETUP_STATE_BEGIN:			// zahájení
    {
      // check if current user privileges allow the run of the setup
#if 0
      // test commandline parameter passing and parsing
      if (GIsRunas)
      {
        MessageBox(NULL, _T("RUNAS ADMIN"), _T("RUNAS ADMIN"), 0);
      }
#endif
      if (!CheckAccessRights())
      {
        if (GIsRunas)
        {
          CString sText = SetupLocalizeString(_T("SETUP_STR_NO_ACCESS"));
          SetupMessageBox(sText, MB_OK | MB_ICONSTOP);
          m_nSetupResult = stpSetupError;
          return SETUP_STATE_FINAL;
        }
        else
        {
          // run as Administrator
          TCHAR sModuleFileName[_MAX_PATH];
          GetModuleFileName(GetModuleHandle(NULL),sModuleFileName,_MAX_PATH);
          TCHAR *cmdline = GetCommandLine();
          TCHAR *cmdlineRest = cmdline + _tcslen(sModuleFileName);
          if ( cmdline[0]==_T('"') ) cmdlineRest = cmdlineRest + 2;

          TCHAR *newLineRest = new TCHAR[_tcslen(cmdlineRest)+32];
          _tcscpy(newLineRest, cmdlineRest);
          _tcscat(newLineRest, _T(" -runas"));

          ShellExecute(NULL, 
              _T("runas"),
              sModuleFileName,
              newLineRest,
              NULL,                        // default dir 
              SW_SHOWNORMAL  
          ); 
          delete newLineRest;
          return SETUP_STATE_FINAL;
        }
      }
    }
		if (!InitSetupParams())
		{	// vnitøní chyba
			return SETUP_STATE_FINAL;	
		}
    if (m_forceInstall!=FI_YES)
		{
			CString regKeyApp = m_sRegistryRoot[0];
			HKEY key;
      // check if application is already instaled
			if (::RegOpenKey(HKEY_LOCAL_MACHINE, regKeyApp, &key) == ERROR_SUCCESS)
			{
        DWORD length = KEY_BYTES;
        CString regName = _T("Key"); 
        m_cdKeyOptions.m_cdKeyIsInRegistry = RegQueryValueEx (key, regName, 0, NULL, (BYTE*) (&m_cdKeyOptions.m_cdKeyFromRegistry[0]), &length) == ERROR_SUCCESS;
        ::RegCloseKey(key);

        // if silent skip wizard
        if (m_forceInstall== FI_SILENT) 
          return SETUP_STATE_COPY_BGN;

        // test whether we should upgrade previously installed (preorder) version
        if (m_upgradeTestFile.GetLength()>0)
        {
          CString dir;
          MakeLongDirStr(dir, m_sSetupPoseidonDir);
          RString testPath = CString2RString(dir + m_upgradeTestFile);
          RString testUninstalledPath = CString2RString(dir + m_uninstalledTestFile);
          if(!QIFileFunctions::FileExists(testPath) && QIFileFunctions::FileExists(testUninstalledPath)) //full version file does not exist => Install must UnInstall first => Upgrade
          {
            CString sText = SetupLocalizeString (_T("SETUP_STR_UPGRADE"));
            if (SetupMessageBox(sText, MB_YESNO | MB_ICONQUESTION) == IDNO)
            {
              m_nSetupResult = stpUserCancel;
              return SETUP_STATE_FINAL;	
            }
            else // Yes
            {
              return SETUP_STATE_UPGRADE;	
            }
          }
        }

				CString sText = SetupLocalizeString (_T("SETUP_STR_ALREADY_INSTALLED"));
				if (SetupMessageBox(sText, MB_YESNO | MB_ICONQUESTION) == IDNO)
				{
					m_nSetupResult = stpUserCancel;
					return SETUP_STATE_FINAL;	
				}
			}
		}

    if (m_showSetupMode)
    {
      CString messageText = _T("Setup mode: ");(m_updaterMode?_T("Updater"):_T("Installer"));
      messageText += _T("\nRun custom exe after end: ");
      messageText += (m_runAfterUpdateFlag?_T("Run after"):_T("Do not run after"));
      SetupMessageBox( messageText );
    }

    // should we act as updater?
#if _ENABLE_DLC_SUPPORT
    if (m_updaterMode)
    {
      // so check for update if update needed
      CString setupPath;
      MakeShortDirStr(setupPath, m_sSetupSourceDir);
      if (!DLCTools::IsCheckNeeded(LPCTSTR2RString(setupPath)))
      {
        // no check needed, end setup.
        return SETUP_STATE_ENDAPP;	
      }
    }
#endif

    if (m_requiredVersions.Size()>0)
    {
      return SETUP_STATE_REQUIRED_VERSION;
    }

    // setup is in regular mode or installation needed
    return SETUP_STATE_STPWIZ;	

    // checked version
  case SETUP_STATE_REQUIRED_VERSION:
    {
      for (int i=0; i<m_requiredVersions.Size(); i++)
      {
        RequiredVersion &required = m_requiredVersions[i];
        HKEY hKey;
        HRESULT hr=::RegOpenKeyEx(HKEY_LOCAL_MACHINE, required.registryKey, 0, KEY_READ, &hKey);
        if (hr!=ERROR_SUCCESS) return SETUP_STATE_ENDAPP;
        CString filename = GetRegistryValue(hKey, required.registryVal);
        if (!filename.GetLength()) return SETUP_STATE_ENDAPP;
        filename = filename + CString("\\") + required.fileName;

        DWORD fileAttr = GetFileAttributes(filename);
        if (0xFFFFFFFF == fileAttr)
        {
          TCHAR errorMsg[512];        
          wsprintf(errorMsg, SetupLocalizeString(_T("SETUP_EXE_ERROR")), filename);

          SetupMessageBox(errorMsg,MB_OK|MB_ICONSTOP);
          return SETUP_STATE_ENDAPP;
        }

        // allocate a block of memory for the version info
        DWORD dummy;
        DWORD dwSize = GetFileVersionInfoSize(filename, &dummy);
        if (dwSize == 0)
        {
          return SETUP_STATE_ENDAPP;
        }

        AutoArray<unsigned char> data;
        data.Realloc(dwSize);
        data.Resize(dwSize);

        // load the version info
        if (!GetFileVersionInfo(filename, NULL, dwSize, data.Data()))
        {
          return SETUP_STATE_ENDAPP;
        }

        UINT uiVerLen = 0;
        // pointer to fixed file info structure
        VS_FIXEDFILEINFO* pFixedInfo = 0;
        // get the fixed file info (language-independent) 
        if(VerQueryValue(&data[0], TEXT("\\"), (void**)&pFixedInfo, (UINT *)&uiVerLen) == 0)
        {
          return SETUP_STATE_ENDAPP;
        }

        int idx = required.version.Find('.');

        if (idx != -1) 
        {
          DWORD productVersionMS = HIWORD (pFixedInfo->dwProductVersionMS);
          DWORD productVersionLS = LOWORD(pFixedInfo->dwProductVersionMS);

          LogF("Installed version: %u.%u.%u.%u", productVersionMS, productVersionLS, HIWORD (pFixedInfo->dwProductVersionLS), LOWORD (pFixedInfo->dwProductVersionLS));

          DWORD requiredProductVersionMS = _ttoi(required.version.Mid(0, idx));
          DWORD requiredProductVersionLS = _ttoi(required.version.Mid(idx + 1));

          LogF("Required version: %u.%u", requiredProductVersionMS, requiredProductVersionLS);

          if (requiredProductVersionMS > productVersionMS || requiredProductVersionLS > productVersionLS)
          {          
            TCHAR title[128];
            wsprintf(title, SetupLocalizeString(_T("SETUP_VERSION_REQUIRED_TITLE")), required.version);
            TCHAR body[256];
            wsprintf(body, required.textMsgBody, required.version);
          
            //wsprintf(body, _T("This product requires version %s to be installed first."), m_sRequiredVersion);

            CString msg = body;
            msg +=  _T("\n");

            MessageBox(NULL, msg + required.textMsg, title, MB_OK | MB_ICONEXCLAMATION);

            return SETUP_STATE_ENDAPP;
          }
        }
      }
    }

    return SETUP_STATE_STPWIZ;

  case SETUP_STATE_UPGRADE:			// Upgrade == UnInstall first and continue with Install after
    {
      // run UnInstall with special upgrade parameters:  #@#uninstall.log -upgrade

      STARTUPINFO stInfo;
      memset (&stInfo, 0, sizeof (stInfo));
      stInfo.cb = sizeof(stInfo);

      PROCESS_INFORMATION pinfo;
      memset (&pinfo, 0, sizeof (pinfo));
      CString dir;
      MakeLongDirStr(dir, m_sSetupPoseidonDir);
      CString wholeName = dir + L"UnInstall.exe";
      CString uninstallCommandLine = CString("\"") + wholeName + "\" #@#uninstall.log -upgrade";
      BOOL succeed = ::CreateProcess (wholeName, uninstallCommandLine.GetBuffer(), NULL, NULL,
        FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
        NULL, dir, &stInfo, &pinfo);

      if (succeed)
      {
        DWORD dwExitCode;
        while (::GetExitCodeProcess(pinfo.hProcess, &dwExitCode) && dwExitCode == STILL_ACTIVE)
        {
          ::Sleep(100);
        }
        RString testPath = CString2RString(dir + m_uninstalledTestFile);
        if(QIFileFunctions::FileExists(testPath))
        {
          m_nSetupResult = stpUserCancel;
          return SETUP_STATE_FINAL;	
        }
      }
      else // unable to run uninstall, so try to install it over the older installation either
      {
        LPVOID lpMsgBuf;
        DWORD dw = GetLastError(); 

        FormatMessage(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | 
          FORMAT_MESSAGE_FROM_SYSTEM,
          NULL,
          dw,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPTSTR) &lpMsgBuf,
          0, NULL );

        RString errorCode = CString2RString(CString((LPTSTR)lpMsgBuf));
        RptF("Error: %s", cc_cast(errorCode));
        m_nSetupResult = stpUserCancel;
        return SETUP_STATE_FINAL;	
      }
    }
    return SETUP_STATE_STPWIZ;	

	case SETUP_STATE_STPWIZ:		// Wizard
    //prepare temporary directory (if there is SETUP_3RD_PARTY_AND_DATA, create Setup Installer Temporary)
    PrepareResourceData();
		if (DoSetupWizard())
		{	// OK - pokracuji
			return SETUP_STATE_FINAL;	
		}	// else - pøerušeno uživatelem
		m_nSetupResult = stpUserCancel;
		return SETUP_STATE_FINAL;	

	case SETUP_STATE_COPY_BGN:		// zahájeno kopírování
		{	// zahájím proces kopírování
			m_nCurrCopySize = 0;
		}
		return SETUP_STATE_COPY_RUN;	
	case SETUP_STATE_COPY_RUN:		// probíhá kopírování
		{	
			CString sText;
			// probíhá kopírování
			if (IsRunningCopy())
				return SETUP_STATE_COPY_RUN; // kopíruji dál
			// byl chyba kopírování
			if (IsLastCopyErr(sText))
			{
        int res = IDABORT;
        if (m_canIgnoreError)
          res = SetupMessageBox(sText,MB_ABORTRETRYIGNORE|MB_ICONSTOP);
        else
				SetupMessageBox(sText,MB_OK|MB_ICONSTOP);

        if (res==IDRETRY)
        {
          //run copy thread again with same setting
          m_sCopyError.Empty();
          DoRunCopyThread();
          return SETUP_STATE_COPY_RUN; // kopíruji znuvo to same
        }
        else if (res==IDIGNORE)
        {
          m_sCopyError.Empty();
          m_nSetupResult = stpSetupErrorIgnored;

          return SETUP_STATE_COPY_RUN; // keep in copying next file
        }
				m_nSetupResult = stpSetupError;
				return SETUP_STATE_FINAL;
			}
			// kopírování dalšího souboru
			if (m_arFileList.GetSize() > 0)
			{
				CSetupFileItem* pItem = (CSetupFileItem*)(m_arFileList[0]);
				m_arFileList.RemoveAt(0);
				if (pItem != NULL)
				{
					if (m_currentCdRom != pItem->m_pCdRom)
					{
						if (!IsCdRomPresent (pItem->m_pCdRom, m_currentDrive, m_sSetupSourceDir))
						{
							// dialog
							CInsertCdRomDlg dlg (this, pItem->m_pCdRom, m_sSetupSourceDir, m_currentDrive);
							if (dlg.DoModal () != IDOK)
							{
								// error
								m_nSetupResult = stpSetupError;
								delete pItem;
								return SETUP_STATE_FINAL;
							}
						}
						m_currentCdRom = pItem->m_pCdRom;
					}
				}
				if (pItem != NULL && pItem->m_isDirectory)
				{
					int i = 0;
					AddDirectory (&pItem->m_arLogFileName, 
						m_currentDrive + m_currentCdRom->_dir,
						pItem->m_sSourceFileName, pItem->m_sDestinationFileName, 
						pItem->m_pCdRom, i,
            pItem->m_sDifferentDest
            );
					delete pItem;
					pItem = (CSetupFileItem*)(m_arFileList[0]);
					m_arFileList.RemoveAt(0);
				}
				if (pItem != NULL && !pItem->m_isDirectory)
				{
          CString sourceFile = pItem->m_sSourceFileName;
          int pos = sourceFile.Find(_T("#"));
          if (pos!=-1 && sourceFile.Mid(pos+1).CompareNoCase((CString)cc_cast(GLanguage))==0)
          {
            pos = pItem->m_sDestinationFileName.Find(_T("#"));
            if (pos!=-1)
              pItem->m_sDestinationFileName =pItem->m_sDestinationFileName.Mid(0,pos);
            pos=-1; 
          }
          
          // check all files and skip files that match language mask  
          if (pos==-1)
          {
            int setupLangCount = sizeof(*g_setupLanguages)/sizeof((*g_setupLanguages)[0]);
            for(int i=0; i<setupLangCount; ++i)
            {
              int extPos = sourceFile.Find(_T('.'));
              if (extPos==-1) extPos = sourceFile.GetLength(); 
              if (sourceFile.Mid(extPos,(*g_setupLanguages)[i].GetLength()).CompareNoCase((*g_setupLanguages)[i])==0) 
              {
                pos=0;
                break;
              }
            }
          }

          if (pos==-1) // if hash mark is in filename this file belongs to only one language distribution    
          {
            //prefer format file.language.ext - same mask is used in install.exe - can share files
            pos = sourceFile.Find(_T('.'));
            if (pos==-1) pos = sourceFile.GetLength(); 

            CString sourceFileLang = sourceFile.Mid(0,pos) + m_sFileLanguageExtension + sourceFile.Mid(pos);
            extern bool FileExists(CString &path);
            if (FileExists(m_sSetupSourceDir+sourceFileLang)) 
              sourceFile = sourceFileLang;
            else
            {
              //localized file can be in file.ext#language format - obsolate, doesn't work on some file systems 
              sourceFileLang = sourceFile+_T("#")+ cc_cast(GLanguage);
              if (FileExists(m_sSetupSourceDir+sourceFileLang)) 
                sourceFile = sourceFileLang;
            }

            CString sDest; 
            CString sDestDir; 
            if (!pItem->m_sDifferentDest.IsEmpty())
              sDestDir = pItem->m_sDifferentDest;
            else
              sDestDir = m_sSetupPoseidonDir;

            if (pItem->m_sDestinationFileName[0]=='\\')
              MakeShortDirStr(sDest,sDestDir);
            else
              MakeLongDirStr(sDest,sDestDir);
		    	sDest += pItem->m_sDestinationFileName;
            if (TestCondition(pItem->m_sCondition,pItem->m_sDifferentDest))
            {
              if (!DoRunCopy(sDest,sourceFile,pItem))
              {
                m_nSetupResult = stpSetupError;
                delete pItem;
                return SETUP_STATE_FINAL;
              };
            };
          };
					delete pItem;
				}
				return SETUP_STATE_COPY_RUN; // kopíruji dál
			}
		}
		return SETUP_STATE_COPY_END;	
	case SETUP_STATE_COPY_END:		// ukonceno kopírování
		{	// ukoncen proces kopírování
			CancelCopy();
		}
		return SETUP_STATE_DIRECTX;
	case SETUP_STATE_DIRECTX:
		{	// ukoncen proces kopírování
      if ((m_processDirectX==DRX_DIALOG && m_bSetupDirectX) || m_processDirectX==DRX_SILENT)
			{
				DoSetupDirectX();
			};
		}
    if (m_waitOnRunningProcess)
    {
      m_nextSetupState = SETUP_STATE_OPEN_AL;
      return SETUP_STATE_WAIT_ON_RUNNING_PROCESS;
    }
    else
      return SETUP_STATE_OPEN_AL;
  case SETUP_STATE_WAIT_ON_RUNNING_PROCESS:
    {
      if (WaitForSingleObject(m_runningProcessInfo.hProcess, 80)!=WAIT_TIMEOUT)
      {
        m_waitOnRunningProcess = false;
        return m_nextSetupState;
      }
    }
    return SETUP_STATE_WAIT_ON_RUNNING_PROCESS; //continue on waiting
  case SETUP_STATE_OPEN_AL:
    {
      if (m_installOpenAL) DoSetupOpenAL();
    }
    return SETUP_STATE_FIREWALL;
  case SETUP_STATE_FIREWALL:
    DoSetupFirewall();
    return SETUP_STATE_SHORTCUTS;
	case SETUP_STATE_SHORTCUTS:
		DoCreateShortcuts();
		return SETUP_STATE_REGISTRY;

  case SETUP_STATE_REGISTRY:
		DoCreateRegistry();
		return SETUP_STATE_GAME_EXPLORER;	

  case SETUP_STATE_GAME_EXPLORER:
    DoRegisterInGameExplorer();
    return SETUP_STATE_RUN_APPS;	

  case SETUP_STATE_RUN_APPS:
    if (!DoRunAplications()) return SETUP_STATE_RUN_APPS;
      CancelRunApp();
      return SETUP_STATE_SETUP_LOG;

	case SETUP_STATE_SETUP_LOG:
    {
      for (int k=0,end=m_updateUnistallAfterInstallEnd.GetCount();k<end;++k)
        for (int i = 0; i < m_arApplLogFileName.GetSize (); ++i)
        {
          AddSetupLogLine(m_arApplLogFileName[i], 'P', m_updateUnistallAfterInstallEnd[k]);
          if (m_updateUnistallAfterInstallEnd[k][0]==_T ('P'))
          {
            AddSetupLogLine(m_arApplLogFileName[i], 'P', CString(_T("M:"))+m_sSetupPoseidonDir);
          }
        }   
		  DoCreateSetupLog();
      DoCreateCheckFileLog();
		  return SETUP_STATE_FINAL;
    }

	case SETUP_STATE_FINAL:
		{
			CString sText;
			switch(m_nSetupResult)
			{
			case stpInternalErr:
				sText = SetupLocalizeString (_T("SETUP_STR_RESULT_INTERERR"));
				SetupMessageBox(sText, MB_OK | MB_ICONEXCLAMATION);
				UndoSetup();
				break;
			case stpUserCancel:
				sText = SetupLocalizeString (_T("SETUP_STR_RESULT_CANCELUSER"));
				SetupMessageBox(sText, MB_OK | MB_ICONEXCLAMATION);
				UndoSetup();
				break;
			case stpSetupError:
				sText = SetupLocalizeString (_T("SETUP_STR_RESULT_ERROR"));
				SetupMessageBox(sText, MB_OK | MB_ICONEXCLAMATION);
				UndoSetup();
				break;
      case stpSetupErrorIgnored:
        sText = SetupLocalizeString (_T("SETUP_STR_RESULT_ERROR"));
        SetupMessageBox(sText, MB_OK | MB_ICONEXCLAMATION);
        break;
			case stpOKRestart:
				sText = SetupLocalizeString (_T("SETUP_STR_RESULT_RESTART"));
				if (SetupMessageBox(sText, MB_YESNO | MB_ICONQUESTION) == IDYES)
				{
					ExitWindowsEx(EWX_REBOOT, 0);
				}
				break;
      case stpDirectXError:
        sText = SetupLocalizeString (_T("SETUP_STR_RESULT_DIRECTX_ERROR"));
        SetupMessageBox(sText, MB_OK | MB_ICONEXCLAMATION);
        //continue as if installation was successful
        m_nSetupResult = stpOK;
			default:
				ASSERT(m_nSetupResult == stpOK);
				sText = SetupLocalizeString (_T("SETUP_STR_RESULT_OK"));
				if (m_showResultOK)
        {
          // show last dialog that installation has been completed
          if (m_arActions.GetSize()!=0) DoResultOK();
        }
        else
        {
          // no final dialog but set all actions as checked to execute them
          for (int i = 0; i < m_arActions.GetSize (); ++i)
            m_arActionChecks[i] = 1;
        }
        bool showEndDialog=false; 
				{
					for (int i = 0; i < m_arActions.GetSize(); ++i)
					{
						CString *action = (CString*) (m_arActions[i]);
            bool isSpecialAction=false;
						if (m_arActionChecks[i] == 1 && NULL != action)
						{
							CString dir;
							switch ((*action)[4])
							{
							case _T ('D'):
							case _T ('d'):
								MakeLongDirStr (dir, m_sSetupPoseidonDir);
								break;

							case _T ('S'):
							case _T ('s'):
								MakeLongDirStr (dir, m_sSetupSourceDir);
								break;
              case _T ('A'):
              case _T ('a'): //special action
                isSpecialAction = true;
                break;
							}

							CString filename;
              CString exename;
							int f = action->Find (_T ('|'), 6);
							if (f > 0)
							{
                exename = action->Mid (6, f - 6);
							}
							else
							{
								exename = action->Mid (6);
							}
              filename = dir + exename;

              if (!isSpecialAction && filename.Find (_T ("%L")) >= 0)
							{
								CString tmpName1 = filename;
								tmpName1.Replace (_T ("%L"), m_sFileLanguageExtension);
								if (_taccess (tmpName1, 00) == 0)
								{
									filename = tmpName1;
								}
								else
								{
									CString tmpName2 = filename;
									tmpName2.Replace (_T ("%L"), _T (""));
									if (_taccess (tmpName2, 00) == 0)
									{
										filename = tmpName2;
									}
									else
									{
										filename = tmpName1;
									}
								}
							}

							switch ((*action)[0])
							{
							case _T ('W'):
							case _T ('w'):
								{
									STARTUPINFO stInfo;
									memset (&stInfo, 0, sizeof (stInfo));
									stInfo.cb = sizeof(stInfo);
									
									PROCESS_INFORMATION prInfo;
									memset (&prInfo, 0, sizeof (prInfo));
									if (::CreateProcess (filename, NULL, NULL, NULL,
										FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS|CREATE_NO_WINDOW,
										NULL, NULL, &stInfo, &prInfo))
									{
										DWORD dwExitCode;
										while (::GetExitCodeProcess(prInfo.hProcess, &dwExitCode) && dwExitCode == STILL_ACTIVE)
										{
											::Sleep(100);
										}
                    showEndDialog=true;
									}
								}
								break;

							case _T ('O'):
							case _T ('o'):
								{
									HINSTANCE code = ShellExecute (NULL, _T("open"), filename, _T(""), dir, SW_SHOWNORMAL);
                  (void)code;
								}
								break;

							case _T ('E'):
							case _T ('e'):
                if (isSpecialAction)
                {
                  if (_tcsicmp(filename, _T("createdesktop"))==0)
                  {
                    CreateDesktopShortcuts();
                    DoCreateSetupLog(); //recreate it (as there should be added desktop icon removal)
                  }
                  else if (_tcsicmp(filename, _T("createsendto"))==0)
                  {
                    CreateSendToShortcuts();
                    DoCreateSetupLog(); 
                  }
                }
                else
                {
                  STARTUPINFO stInfo;
                  memset (&stInfo, 0, sizeof (stInfo));
                  stInfo.cb = sizeof(stInfo);

                  PROCESS_INFORMATION prInfo;
                  memset (&prInfo, 0, sizeof (prInfo));
                  SetCurrentDirectory (dir);
                  CString argsAlone = _T("");
                  CString useCommandLine = "";
                  CString uz = _T("\"");
                  for (int ix=0; ix<exename.GetLength(); ix++)
                  {
                    if (exename[ix]==_T(' '))
                    {
                      argsAlone = exename.Mid(ix+1);
                      exename = exename.Mid(0, ix);
                      useCommandLine = uz + dir + exename + CString(_T("\" ")) + argsAlone;
                      break;
                    }
                  }
                  CString appNameToRun = dir + exename;
                  ::CreateProcess (appNameToRun, useCommandLine.GetBuffer(), NULL, NULL,
                    FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS |CREATE_NO_WINDOW,
                    NULL, dir, &stInfo, &prInfo);
/*
                  SetCurrentDirectory (dir); 
								  WinExec (CString2RString(filename), SW_SHOW);
*/
                }
								break;
							}
						}
					}

#if _ENABLE_DLC_SUPPORT
          // if enabled in setup.dat, save version in installedVersion file
          if (m_saveVersionFile)
            DLCTools::SaveCurrentVersionFile(LPCTSTR2RString(m_sSetupSourceDir));
#endif
          if (showEndDialog&&m_showResultOK)
          {
				    sText = SetupLocalizeString (_T("SETUP_STR_RESULT_OK"));
            SetupMessageBox(sText, MB_OK );
          }

				}

/*
				if (m_bGameSpy)
				{
					CString filename = m_sSetupSourceDir + "data\\ArcadeInstallOPFLASH107g.exe";
			
					STARTUPINFO stInfo;
					memset(&stInfo, 0, sizeof(stInfo));
					stInfo.cb = sizeof(stInfo);

					PROCESS_INFORMATION prInfo;
					memset(&prInfo, 0, sizeof(prInfo));
					if
					(
						::CreateProcess
						(
							filename, NULL, NULL, NULL,
							FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
							NULL, NULL, &stInfo, &prInfo
						)
					)
					{
						DWORD dwExitCode;
						while (::GetExitCodeProcess(prInfo.hProcess, &dwExitCode) && dwExitCode == STILL_ACTIVE)
						{
							::Sleep(100);
						}
					}
				}
				CString dir; MakeLongDirStr(dir,m_sSetupPoseidonDir);

				if (m_bReadMe)
				{
					CString filename;
					switch (PRIMARYLANGID(GetUserDefaultLangID()))
					{
					case LANG_FRENCH:
						filename = dir + "readme.french.chm";
						break;
					case LANG_SPANISH:
						filename = dir + "readme.spanish.chm";
						break;
					case LANG_ITALIAN:
						filename = dir + "readme.italian.chm";
						break;
					case LANG_GERMAN:
						filename = dir + "readme.german.chm";
						break;
					case LANG_POLISH:
						filename = dir + "readme.polish.chm";
						break;
					default:
						filename = dir + "readme.chm";
						break;
					}
					ShellExecute(NULL, "open", filename, "", dir, SW_SHOWNORMAL);
				}

				if (m_bRegistration)
				{
					CString url = "http://www.bistudio.com/";
					ShellExecute(NULL, "open", url, "", dir, SW_SHOWNORMAL);
				}
				
				if (m_bRunApp)
				{
#if _DEMO
					CString filename = dir + "OpFlashPreferencesDemo.exe";
#elif _MP_DEMO
					CString filename = dir + "OpFlashPreferencesMPDemo.exe";
#else
					CString filename = dir + "OpFlashPreferences.exe";
#endif
					SetCurrentDirectory(dir); 
					WinExec(filename, SW_SHOW);
				}
*/

//				SetupMessageBox(sText, MB_OK | MB_ICONINFORMATION);
				break;
			};
		}
    // remove temporary files U:F:xxxx.xxx
    CString dest;
    MakeLongDirStr(dest,m_sSetupPoseidonDir);
    for (int i=0,end=m_delFilesAfterInstallEnd.GetCount();i<end;++i)
      _tremove(dest+m_delFilesAfterInstallEnd[i]);
    // remove temporary directories U:S:xxxx
    for (int i=0,end=m_delDirsAfterInstallEnd.GetCount();i<end;++i)
      _trmdir(dest+m_delDirsAfterInstallEnd[i]);
    // remove temporary directory
    CString tempDir = GetTempDirectory(m_sApplicationName);
    if (tempDir.GetLength()>0) DeleteDirectoryStructure(CString2RString(tempDir));
    
		
    return SETUP_STATE_ENDAPP;
	};

	ASSERT(FALSE);
	return SETUP_STATE_FINAL;
};



/////////////////////////////////////////////////////////////////////////////
// inicializace dat

typedef int (CALLBACK* GETVERSION)(DWORD *, DWORD *);

BOOL CSetupObject::InitSetupParams()
{
	switch (PRIMARYLANGID (GetUserDefaultLangID ()))
	{
	case LANG_FRENCH:
		m_sFileLanguageExtension = (*g_setupLanguages)[0];
		break;
	case LANG_SPANISH:
		m_sFileLanguageExtension = (*g_setupLanguages)[1];
		break;
	case LANG_ITALIAN:
		m_sFileLanguageExtension = (*g_setupLanguages)[2];
		break;
	case LANG_GERMAN:
		m_sFileLanguageExtension = (*g_setupLanguages)[3];
		break;
	case LANG_POLISH:
		m_sFileLanguageExtension = (*g_setupLanguages)[4];
		break;
	case LANG_CZECH:
		m_sFileLanguageExtension = (*g_setupLanguages)[5];
		break;
	case LANG_RUSSIAN:
		m_sFileLanguageExtension = (*g_setupLanguages)[6];
		break;
	case LANG_HUNGARIAN:
		m_sFileLanguageExtension = (*g_setupLanguages)[7];
		break;

	default:
		m_sFileLanguageExtension = "";
		break;
	}

	CWaitCursor	WaitCursor;

	LPTSTR setupSourceDir = m_sSetupSourceDir.GetBuffer(_MAX_PATH);
	GetCurrentDirectory(_MAX_PATH, setupSourceDir);
	m_sSetupSourceDir.ReleaseBuffer();
	MakeLongDirStr(m_sSetupSourceDir, m_sSetupSourceDir);

	// parametrický soubor pro Setup (Setup.dat)
	m_sSetupDatFile  = m_sSetupSourceDir;
	m_sSetupDatFile += SETUP_DAT_FILE_NAME;
	// nacte parametrický soubor pro setup
	if (!LoadSetupDatFile())
		return FALSE;
	// vytvoøí seznam souborù ke kopírování
	if (!BuildFileList())
	{
		m_nSetupResult = stpSetupError;
		return FALSE;
	}

  // check if there is an parent key in registry and it's valid
  if (!m_sParentAppRegistryRoot.IsEmpty() && m_sParentAppRegistryRoot!="-")
  {
    // first of all, find key in registry and load it
    unsigned char parentKey[KEY_BYTES];
    if (!ReadRegistryKey(m_sParentAppRegistryRoot, parentKey))
    {
      SetupMessageBox(CSetupObject::SetupLocalizeString (_T("SETUP_STR_CANT_FIND_REGISTRY_SN")));
      m_nSetupResult = stpSetupError;
      return FALSE;
    }

    if (m_iParentProjectId>0)
    {
      // check that the key is valid and belongs to the project defined in setup.dat (projectID)
      if (!CheckCustomCDKey(m_iParentProjectId, parentKey, NULL, 0, NULL))
      {
        SetupMessageBox(CSetupObject::SetupLocalizeString (_T("SETUP_STR_WRONG_PARENT_SN_PROJECT_ID")));
        m_nSetupResult = stpSetupError;
        return FALSE;
      }
    }

    // check that key is not blacklisted
    if (IsSerialInBlackList(parentKey))
    {
      SetupMessageBox(CSetupObject::SetupLocalizeString (_T("SETUP_STR_BLACK_LISTED_PARENT_SN")));
      m_nSetupResult = stpSetupError;
      return FALSE;
    }
  }

	// Direct X - aktuální verze
	m_bSetupDirectX = TRUE;
	m_dwVersion		=
	m_dwRevision	= 0;

// Call the function explicitly
//	int nRes = DirectXSetupGetVersion(&m_dwVersion,&m_dwRevision);

	CString sDLLName;
	MakeLongDirStr(sDLLName, m_sSetupSourceDir);
	sDLLName += DIRECTX_SUBDIRECTORY;
	sDLLName += "\\dsetup.dll";

	HINSTANCE hDLL = LoadLibrary(sDLLName);
	GETVERSION GetVersion;
	int nRes = 0;

	if (hDLL != NULL)
	{
		GetVersion = (GETVERSION)GetProcAddress(hDLL, "DirectXSetupGetVersion");
		if (GetVersion)
		{
			nRes = GetVersion(&m_dwVersion, &m_dwRevision);
		}
		FreeLibrary(hDLL);       
	}

// Do not install DirectX in demo
#if !_DEMO && !_MP_DEMO
	if (nRes == 0)
	{
		m_bSetupDirectX = TRUE;
		m_dwVersion		=
		m_dwRevision	= 0;
	} else
	{
		if (m_dwVersion< 0x40008)
			m_bSetupDirectX = TRUE;
	};
#endif

	// úspìšná inicializace
	return TRUE;
};

CString UTF8ToCString(LPCSTR buff)
{
#if UNICODE
  Temp<wchar_t> bufW;
  {
    int wSize = MultiByteToWideChar(CP_UTF8, 0, buff, -1, NULL, 0);
    bufW.Realloc(wSize);
    MultiByteToWideChar(CP_UTF8, 0, buff, -1, bufW.Data(), wSize);
  }
  return CString(bufW);
#else
  return CString(buff);
#endif
}

#define CDKEY_BLACKLIST(XX) \
  XX(883458) \
  XX(537514689) \
  XX(805583236) \
  XX(1879660678) \

#define CHECK_CDKEY(x) if ((x) == keyValue) found = true;

BOOL CSetupObject::IsSerialInBlackList(unsigned char sn[KEY_BYTES]) const
{
  // check that number is not in the black list
  bool found = false;
  __int64 keyValue = *(unsigned int *)sn;
  CDKEY_BLACKLIST(CHECK_CDKEY);

  return found;
}

#include <El/DataSignatures/dataSignatures.hpp>
#include <Wincrypt.h>

static bool AcquireContext(HCRYPTPROV *provider)
{
  // FIX: access was not always granted to the private key, but we need only the public key
  if (CryptAcquireContext(provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return true;
  // create a new key container
  if (GetLastError() == NTE_BAD_KEYSET)
  {
    if (CryptAcquireContext(provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET)) return true;
  }
  return false;
}

int VerifySignature(const void *data, int size, const DSSignature &signature)
{
  HCRYPTPROV provider = NULL;
  HCRYPTKEY key = NULL;
  HCRYPTHASH hash = NULL;

  if (!AcquireContext(&provider))
  {
    return 3; // error #    
  }

  // import the public key
  if (!CryptImportKey(
    provider, (const BYTE *)signature._key._content.Data(), signature._key._content.Size(), NULL, 0, &key))
  {
    if (provider) CryptReleaseContext(provider, 0);
    return 4; // error #
  }

  // create the hash
  if (!CryptCreateHash(provider, CALG_SHA, NULL, 0, &hash))
  {
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return 5; // error #
  }

  // calculate the hash
  if (!CryptHashData(hash, (BYTE *)data, size, 0))
  {
    if (hash) CryptDestroyHash(hash);
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return 6; // error #
  }

  // check the signature
  bool ok = CryptVerifySignature(hash, (BYTE *)signature._content1.Data(), signature._content1.Size(), key, NULL, 0) == TRUE; 

  if (hash) CryptDestroyHash(hash);
  if (key) CryptDestroyKey(key);
  if (provider) CryptReleaseContext(provider, 0);

  return ok ? 0 : 7; // error #
}

BOOL CSetupObject::LoadSetupDatFile()
{
  BOOL reportErrs = FALSE;
  char *setupDat = NULL;
  char *setupDatSign = NULL;

  DSSignature signature;

  int sizeSignature;

  Temp<char> tempbuf; 
  Temp<char> tempbufSign; 
  int size = 0;
  //we will try to read setup.dat data from setup.exe resources
  setupDat = (char *)FindAndLoadResource(size, this, SETUP_DAT_FILE_NAME);

  RString setupDatSignName = RString(SETUP_DAT_FILE_NAME) + RString(".bisign"); 
  setupDatSign = (char *)FindAndLoadResource(sizeSignature, this, cc_cast(setupDatSignName));
  if (setupDatSign)
  {
    QIStrStream buf(setupDatSign, sizeSignature);
    SerializeBinStream stream(&buf);
    signature.SerializeBin(stream);
  }

  if (setupDat==NULL && VerifyFileRead(m_sSetupDatFile,reportErrs))
  {
    CString sSetupDatSignFile = m_sSetupDatFile + _T(".bisign");
    if (!VerifyFileRead(sSetupDatSignFile,reportErrs))
    {
      ReportFileErrorRead(sSetupDatSignFile,TRUE);
      return FALSE;
    }
    else
    {
      signature.Load(LPCTSTR2RString(sSetupDatSignFile));
    }

    CFile  file;
    TRY
    {
      size = QIFileFunctions::GetFileSize(LPCTSTR2RString(m_sSetupDatFile));
      file.Open(m_sSetupDatFile,CFile::modeRead);
      tempbuf.Realloc(size);
      setupDat = tempbuf;
      file.Read(setupDat,size);
      file.Close();
    }
    CATCH_ALL(e)
    {
      ReportFileErrorRead(m_sSetupDatFile,TRUE);
      file.Abort();
      return FALSE;
    }
    END_CATCH_ALL
  }
  if (!setupDat)
  {
    ReportFileErrorRead(m_sSetupDatFile,TRUE);
    return FALSE;
  }

  // check the signature of "setup.dat"
#if REINFORCEMENTS
  static const unsigned char keyContent[] = {
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31, 0x00, 0x04, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0xE9, 0xFA, 0x88, 0x75, 0x6F, 0x66, 0x41, 0xC7, 0x64, 0xD0, 0xE9, 0x78,
    0xCD, 0x71, 0xAB, 0x35, 0xD8, 0xE7, 0x25, 0xBF, 0x8D, 0x52, 0x31, 0xD3, 0x5C, 0xFF, 0x52, 0x36,
    0x73, 0xCE, 0xBA, 0x6F, 0xBA, 0x3F, 0x23, 0x3C, 0x70, 0x9E, 0x26, 0x9B, 0x5A, 0x70, 0xD0, 0x1F,
    0x1F, 0x6E, 0xA5, 0xF8, 0xE6, 0x0C, 0x13, 0xD3, 0x62, 0xB2, 0x0C, 0x4E, 0xDB, 0x11, 0xBA, 0x2D,
    0x70, 0xF0, 0x80, 0x9E, 0x79, 0xC6, 0x93, 0xA0, 0x5D, 0xE1, 0x0E, 0x2E, 0xD1, 0xF9, 0x70, 0x0C,
    0xD5, 0xDF, 0x7A, 0x9F, 0xB7, 0xD8, 0x25, 0x80, 0x67, 0xD3, 0xEE, 0x9D, 0xF1, 0xE5, 0x64, 0xF8,
    0xFD, 0x27, 0x2A, 0x0E, 0xC4, 0x77, 0x45, 0xFD, 0xE2, 0xBD, 0x72, 0xF5, 0x01, 0x11, 0xDB, 0xF4,
    0xA9, 0x20, 0x1F, 0xF1, 0xD3, 0xC7, 0x4B, 0x7A, 0x33, 0xF4, 0x95, 0xE8, 0x3A, 0x2E, 0xEC, 0x24,
    0x47, 0xBE, 0xF3, 0xDA  
  };
#else
  static const unsigned char keyContent[] = {
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31, 0x00, 0x04, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x45, 0xD7, 0xCF, 0xC7, 0x5C, 0x06, 0xAB, 0x0A, 0x4F, 0x7F, 0xF9, 0x87,
    0x35, 0xB7, 0x88, 0xD9, 0x17, 0xC1, 0x02, 0x78, 0x39, 0x98, 0x34, 0x06, 0xCB, 0x17, 0x50, 0xC7,
    0x28, 0xE6, 0x3B, 0x40, 0x39, 0xFB, 0x25, 0xB2, 0xDA, 0x65, 0xEF, 0x90, 0xB3, 0xD6, 0x6D, 0xA4,
    0xFB, 0xDF, 0x5B, 0x92, 0x2D, 0x38, 0x73, 0xC5, 0x33, 0xCA, 0x0C, 0x60, 0x91, 0xCC, 0x9C, 0xAB,
    0x6C, 0x67, 0x8A, 0x4F, 0x72, 0x7E, 0xE5, 0xE7, 0x3C, 0xB3, 0xF2, 0x04, 0xF5, 0x2F, 0xD0, 0x74,
    0x1C, 0xE9, 0x93, 0x09, 0xBB, 0x1D, 0x14, 0x33, 0x34, 0xFE, 0xAF, 0x7B, 0x37, 0x79, 0x26, 0xE1,
    0x7B, 0x33, 0xEB, 0x45, 0x0F, 0x22, 0xEA, 0x57, 0xCF, 0x55, 0x69, 0xB3, 0xCA, 0xF8, 0x03, 0x30,
    0xEF, 0x0F, 0x89, 0xD5, 0x66, 0xD1, 0xB6, 0x2B, 0x9F, 0x73, 0x37, 0x09, 0x1C, 0x31, 0x2B, 0x64,
    0xAC, 0xF3, 0x80, 0xCF};
#endif

  signature._key._content.Realloc(sizeof(keyContent));
  memcpy(signature._key._content.Data(), keyContent, sizeof(keyContent));

  // check signature
  if (signature._version<=1 || VerifySignature(setupDat, size, signature)!=0)
  {
    ReportFileErrorRead(m_sSetupDatFile,TRUE);
    return 1;
  }

  int  nIndx = 0;
  char buff[1000];
  //the following part is basically the same as in the file.Read(buff+nIndx,... branch
  int ix=SkipUnicodePrefix(setupDat);
  while(ix<size)
  {
    buff[nIndx]=setupDat[ix++];
    if ((buff[nIndx] == '\n')&&(buff[nIndx-1] == '\r'))
    {
      buff[nIndx-1] = 0;
      nIndx = 0;
      // uchovám øádku
      if (strlen(buff) > 0)
        m_SetupDatLines.Add(UTF8ToCString(buff));
    } else
      nIndx++;
  }
  if (nIndx>0) //we should add the last line too
  {
    buff[nIndx]=0;
    if (strlen(buff) > 0)
      m_SetupDatLines.Add(UTF8ToCString(buff));
  }
  for (int i = 0; i < m_SetupDatLines.GetSize (); ++i)
  {
    ParseSetupDatLine(i); //should be called before the ApplyApplicationNameAndStringTable!
    ApplyApplicationNameAndStringTable (m_SetupDatLines[i], false);
  }
  return TRUE;
};

void CSetupObject::ParseSetupDatLine(int ix)
{
  int lineNum = m_parsedSetupDatLines.Add();
  Assert(lineNum==ix);
  AutoArray<CStringWrp> &setupDatLine = m_parsedSetupDatLines.Set(lineNum);
  
  CString sSource,sDest;
  CString sLine = m_SetupDatLines[ix]; 
  sDest = sLine;
  while (GetFileParamsSubstrings(sLine,sSource,sDest))
  {
    ApplyApplicationNameAndStringTable(sSource, false);
    setupDatLine.Add(sSource);
    sLine = sDest;
  }
  ApplyApplicationNameAndStringTable(sDest, false);
  setupDatLine.Add(sDest); //this should be done in any case. Even empty values should be saved
}

BOOL CSetupObject::GetFileParamsSubstrings(LPCTSTR pLine,CString& sSource,CString& sDest) const
{
	CString sLine = pLine;
	int nLn = sLine.GetLength();
	int nDv = sLine.Find(_T (':'));
	if (nDv < 0) return FALSE;

	if (nDv > 0)
		sSource = sLine.Left(nDv);
	else
		sSource.Empty();

	if (nDv < (nLn-1))
		sDest = sLine.Right(nLn-(nDv+1));
	else
		sDest.Empty();
	return TRUE;
};

void CSetupObject::LoadSetupFileList()
{
  if (!m_SetupFileListLoaded)
  {
    int size;
    char *flist = (char *)FindAndLoadResource(size, this, "SETUP_FILE_LIST");
    if (!flist) return;
    QIStrStream fileListIn(flist, size);
    SerializeBinStream stream(&fileListIn);
    stream << m_SetupFileList; //this is Load!
    m_SetupFileListLoaded = true;
  }
}

int CSetupObject::GetWhereSaved(LPCTSTR filename)
{
  LoadSetupFileList();
  if (!m_SetupFileListLoaded) return -1;
  for (int i=0; i<m_SetupFileList.Size(); i++)
  {
    if (stricmp(CString2RString(CString(filename)), m_SetupFileList[i].name)==0)
    {
      return m_SetupFileList[i].whereSaved;
    }
  }
  return -1;
}

bool CSetupObject::IsSetupBinUsed()
{
  LoadSetupFileList();
  if (!m_SetupFileListLoaded) return false;
  for (int i=0; i<m_SetupFileList.Size(); i++)
  {
    if (m_SetupFileList[i].whereSaved>0) return true;
  }
  return false;
}

CString CSetupObject::GetSystemPath(CString name, CString path)
{
  CString outDir;
  if (name.CompareNoCase(_T("documents"))==0)
  {
    HKEY key;
    TCHAR mainVal[_MAX_PATH];
    DWORD mainValLength = _MAX_PATH;
    bool mainValSet = false;
    if (RegOpenKey (HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders"), &key) == ERROR_SUCCESS)
    {
      if (RegQueryValueEx (key, _T("Personal"), 0, NULL, (BYTE*) (&mainVal[0]), &mainValLength) == ERROR_SUCCESS)
        mainValSet = true;
      RegCloseKey (key);
    }
    if (!mainValSet) lstrcpy(mainVal,_T("%ALLUSERSPROFILE%\\My Documents"));
    MakeLongDirStr (outDir, mainVal);
    outDir += path;
    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);
  }
  else if (name.CompareNoCase(_T("allusersdocuments"))==0)
  {
    CoInitialize(NULL);
    TCHAR mainVal[_MAX_PATH];


    bool mainValSet = false;
/*    
    LPITEMIDLIST	ppidl;
    CWnd *wnd=AfxGetMainWnd();
    HWND hWnd=wnd ? wnd->m_hWnd : NULL;

    HRESULT hres = SHGetSpecialFolderLocation(hWnd, CSIDL_COMMON_DOCUMENTS, &ppidl);
    if (SUCCEEDED(hres))
    {
            SHGetPathFromIDList( ppidl, mainVal );
      mainValSet = true;
    }*/ 
    // SHGetSpecialFolderLocation and SHGetFolderPath returns different values on some Win XP, Carrier Command is using SHGetFolderPath

    if(S_OK == ::SHGetFolderPath(NULL, CSIDL_COMMON_DOCUMENTS, NULL, SHGFP_TYPE_CURRENT, mainVal))
    {
      mainValSet = true;
    }

    if (!mainValSet) lstrcpy(mainVal,_T("%userprofile%\\My Documents"));
    MakeLongDirStr (outDir, mainVal);
    outDir += path;
    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);

    CoUninitialize();
  }
  else if (name.CompareNoCase(_T("desktop"))==0)
  {
    HKEY key;
    TCHAR mainVal[_MAX_PATH];
    DWORD mainValLength = _MAX_PATH;
    bool mainValSet = false;
    if (RegOpenKey (HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders"), &key) == ERROR_SUCCESS)
    {
      if (RegQueryValueEx (key, _T("Desktop"), 0, NULL, (BYTE*) (&mainVal[0]), &mainValLength) == ERROR_SUCCESS)
        mainValSet = true;
      RegCloseKey (key);
    }
    if (!mainValSet) lstrcpy(mainVal,_T("%userprofile%\\Desktop"));
    MakeLongDirStr (outDir, mainVal);
    outDir += path;
    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);
  }
  else if ((name.CompareNoCase(_T("temp"))==0)||(name.CompareNoCase(_T("tmp"))==0))
  {
    TCHAR mainVal[_MAX_PATH];    
    outDir =CString(_T("%temp%\\"))+ path;
    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);
  }
  else if ((name.CompareNoCase(_T("S"))==0)||(name.CompareNoCase(_T("programs"))==0)||(name.CompareNoCase(_T("programfiles"))==0))
  {
    TCHAR mainVal[_MAX_PATH];    
    outDir =CString(_T("%ProgramFiles%\\"))+ path;
    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);
  }
  else if ((name.CompareNoCase(_T("Sx86"))==0)||(name.CompareNoCase(_T("programs(x86)"))==0)||(name.CompareNoCase(_T("programfiles(x86)"))==0))
  {
    TCHAR mainVal[_MAX_PATH]; 
    if (GetEnvironmentVariable(_T("ProgramFiles(x86)"),mainVal,_MAX_PATH))
    {
      outDir =CString(_T("%ProgramFiles(x86)%\\"))+ path;
    }
    else
    {
      outDir =CString(_T("%ProgramFiles%\\"))+ path;
    }

    ExpandEnvironmentStrings (outDir.GetBuffer(), mainVal, _MAX_PATH);
    outDir.ReleaseBuffer();
    MakeShortDirStr (outDir, mainVal);
  }
  else if (name.CompareNoCase(_T("N"))==0)
  {
    TCHAR outStr[_MAX_PATH];
    ExpandEnvironmentStrings (path, outStr, _MAX_PATH);
    outDir = outStr;
  }
  else outDir = path;

  return outDir;
}

void CSetupObject::AddDirectory(CStringArray *logFile, CString srcPath, CString src, CString dst, CCdRom *curCdRom, int &position, CString sCondition, CString sDiffDest)
{
  //We are hopefully installing from large bin files (or one large single setup exe)
  LoadSetupFileList();
  if (m_SetupFileList.Size()!=0)
  {
    for (int i=0; i<m_SetupFileList.Size(); i++)
    {
      RString srcR = CString2RString(src);
      if (strnicmp(m_SetupFileList[i].name, srcR, srcR.GetLength())==0)
      {
        CSetupFileItem* pItem = new CSetupFileItem(logFile);
        pItem->m_sSourceFileName = m_SetupFileList[i].name;
        pItem->m_sDestinationFileName = dst;
        pItem->m_sDestinationFileName+= cc_cast(m_SetupFileList[i].name)+src.GetLength();
        pItem->m_sDestinationFileName = ClearCompressionExt(pItem->m_sDestinationFileName);
        pItem->m_pCdRom = curCdRom;
        pItem->whereSaved = m_SetupFileList[i].whereSaved;
        pItem->savedFileSize = m_SetupFileList[i].len;
        pItem->m_sCondition = sCondition;
        pItem->m_sDifferentDest = sDiffDest;
        m_arFileList.InsertAt (position, pItem);
        ++position;
      }
    }
  }
  else
  {
    CString sPath = srcPath + src;
	  sPath += "\\*.*";
	  WIN32_FIND_DATA	findData;
	  HANDLE h = FindFirstFile(sPath, &findData);
	  if (h != INVALID_HANDLE_VALUE)
	  {
		  do
		  {
			  if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			  {
				  // file
				  CSetupFileItem* pItem = new CSetupFileItem(logFile);
				  pItem->m_sSourceFileName  = src;
				  pItem->m_sSourceFileName += "\\";
				  pItem->m_sSourceFileName += findData.cFileName;
				  pItem->m_sDestinationFileName = dst;
				  pItem->m_sDestinationFileName+= "\\";
				  pItem->m_sDestinationFileName+= ClearCompressionExt(findData.cFileName);
				  pItem->m_pCdRom = curCdRom;
          pItem->m_sCondition = sCondition;
          pItem->m_sDifferentDest = sDiffDest;
				  m_arFileList.InsertAt (position, pItem);
				  ++position;
			  }
			  else if (findData.cFileName[0] != _T ('.'))
			  {
				  // subdirectory
				  CString newSrc = src;
				  newSrc += "\\";
				  newSrc += findData.cFileName;
				  CString newDst = dst;
				  newDst += "\\";
				  newDst += findData.cFileName;
				  AddDirectory(logFile, srcPath, newSrc, newDst, curCdRom, position,sCondition,sDiffDest);
			  }
		  }
		  while (FindNextFile(h, &findData));
	  }
  }
}; 

BOOL CSetupObject::BuildFileList()
{
	m_TotalCopySize = 0;	// spocítám i kompletní délku souborù
	for(int i=0; i<m_parsedSetupDatLines.Size(); i++)
	{
		const ParsedLine &parsedLine = m_parsedSetupDatLines.Get(i);
    Assert(parsedLine.Size()>0);
    Assert(parsedLine[0].GetLength()>0);
    if (parsedLine[0].GetLength()==0) continue;
    CString wholeLine = m_SetupDatLines[i]; //only DEBUG FKJSL:DJKL:JALKFJL:AJSFKL:AJL:FJAL:FJK
    switch(parsedLine.GetChar(0))
		{
    case _T ('/'): //this line is comment (but it is recommended to use double //
      if (parsedLine.Size()<2 || parsedLine.GetChar(1)!=_T('/')) 
      {
        if (m_debugMode)
        {
          CString warnInfo; warnInfo.Format(_T("Warning: The following setup.dat line was not recognized as command\n\n%s"), parsedLine.ConcatenateRest(0));
          SetupMessageBox(warnInfo);
          break;
        }
      }
    case _T (';'): //this line is comment
      break;
		case _T ('D'):	// celý adresáø
			{
				if (parsedLine.Size()>=3)
				{
          int pos=0;
          CString cmd = parsedLine.GetString(pos++);
          CStringArray *arCurrentLogFile = &m_arCurrentLogFile;
          int nextch = 1;
          if (cmd.GetLength()>nextch && cmd[nextch]==_T('N'))
          {
            arCurrentLogFile = NULL; //no uninstalling for this directory and its content
            ++nextch;
          }
          CString sSource;
          CString sDest;
          CString sCondition;
          CString sDestination;

          if (cmd.GetLength()>nextch && cmd[nextch]==_T('P'))
          {
            CString name = parsedLine.GetString(pos++);
            CString path = parsedLine.GetString(pos++);
            sDestination = GetSystemPath(name,path);
            ++nextch;
          }
          if (cmd.GetLength()>nextch && cmd[nextch]==_T('C'))
          {
            sSource = parsedLine.GetString(pos++);
            sDest = parsedLine.GetString(pos++);
            sCondition = parsedLine.ConcatenateRest(pos++);
          }
          else
          {
            sSource = parsedLine.GetString(pos++);
            sDest = parsedLine.ConcatenateRest(pos++);
          }

					if (m_currentCdRom == NULL)
					{
						int i = m_arFileList.GetSize ();
						AddDirectory(arCurrentLogFile, m_sSetupSourceDir, sSource, sDest, NULL, i,sCondition,sDestination);
					}
					else
					{
            LoadSetupFileList();
            if (m_SetupFileList.Size())
            {
              int i = m_arFileList.GetSize ();
              CString discSource = m_currentCdRom->_label + CString("\\") + sSource;
              AddDirectory(arCurrentLogFile, m_sSetupSourceDir, discSource, sDest, NULL, i,sCondition,sDestination);
            }
            else
            {
						  CSetupFileItem* pItem = new CSetupFileItem(arCurrentLogFile);
						  if (pItem == NULL)
						  {
							  return FALSE;
						  }
						  pItem->m_sSourceFileName = sSource;
						  pItem->m_sDestinationFileName = sDest;
						  pItem->m_pCdRom = m_currentCdRom;
						  pItem->m_isDirectory = true;
              pItem->m_sCondition = sCondition;
              pItem->m_sDifferentDest =sDestination;

						  m_arFileList.Add(pItem);
            }
					}
				};
			}; break;
    case _T ('Q'):	// special command
      {
        if (parsedLine.Size()>=3)
        {
          CString sSource = parsedLine.GetString(1);
          CString sDest = parsedLine.ConcatenateRest(2);
                    
          if (_tcsicmp(sSource,_T("directx"))==0)
          {
            if ( _tcsicmp(sDest,_T("silent"))==0 ) m_processDirectX=DRX_SILENT;
            else if (_tcsicmp(sDest,_T("no"))==0 ) m_processDirectX=DRX_NO;
            else if (_tcsicmp(sDest,_T("dialog"))==0 ) m_processDirectX=DRX_DIALOG;
          }
          else if (_tcsicmp(sSource, _T("gameexplorer")) == 0)
          {
            // Game Explorer GDF resource placement
            m_geResource = sDest;
          }
          else if (_tcsicmp(sSource, _T("geplaytask")) == 0)
          {
            // Game Explorer play task definition
            m_gePlayTasks.Add(sDest);
          }
          else if (_tcsicmp(sSource, _T("gesupporttask")) == 0)
          {
            // Game Explorer support task definition
            m_geSupportTasks.Add(sDest);
          }
          else if (_tcsicmp(sSource, _T("filetype")) == 0)
          {
            // File type association definition
            m_fileTypes.Add(sDest);
          }
          else if (_tcsicmp(sSource, _T("filetypeex")) == 0)
          {
            // Extended file type association definition
            // - multiple application can be registered to a single extension
            // - what application to launch is decided by the product info contained by the data file
            // - what application contains the latest loader is decided by the supported file version
            m_fileTypesEx.Add(sDest);
          }
          else if (_tcsicmp(sSource, _T("registry")) == 0)
          {
            // Key to insert into Registry
            SetupRegistryKey setupReg;
            if (setupReg.Init(sDest))
              m_registryKeys.Add(setupReg);
            //else TODO some report that setup.dat is corrupted?
          }
          else if (_tcsicmp(sSource, _T("testVolumeLabel")) == 0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_testVolumeLabel = false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_testVolumeLabel = true;
          }
          else if (_tcsicmp(sSource, _T("openAL")) == 0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_installOpenAL = false;
            else if (_tcsicmp(sDest,_T("silent"))==0 ) m_installOpenAL = true;
          }
          else if (_tcsicmp(sSource, _T("createVersionFile")) == 0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_saveVersionFile = false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_saveVersionFile = true;
          }
          else if (_tcsicmp(sSource, _T("firewall")) == 0)
          {
            _firewall.Add(sDest);
          }
          else if (_tcsicmp(sSource, _T("harddest")) == 0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_doNotAskToDestFolder=false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_doNotAskToDestFolder=true;
          }
          else if (_tcsicmp(sSource,_T("license"))==0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_processLicense=false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_processLicense=true;
          }
          else if (_tcsicmp(sSource,_T("welcome"))==0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_showWelcome=false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_showWelcome=true;
          }
          else if (_tcsicmp(sSource,_T("showresultok"))==0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_showResultOK=false;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_showResultOK=true;
          }
          else if (_tcsicmp(sSource,_T("forceinstall"))==0)
          {
            if (_tcsicmp(sDest,_T("no"))==0 ) m_forceInstall=FI_NO;
            else if (_tcsicmp(sDest,_T("yes"))==0 ) m_forceInstall=FI_YES;
            else if (_tcsicmp(sDest,_T("silent"))==0 ) m_forceInstall=FI_SILENT;
          }
          else if (_tcsicmp(sSource,_T("langprefix"))==0)
          {
            if (_tcsicmp(sDest,_T("short"))==0 )
              g_setupLanguages = &g_setupLanguagesShort;

          }
          else if (_tcsicmp(sSource,_T("info"))==0)
          {
            if (_tcsstr(sDest,_T("yes"))!=0 ) m_processInfo=true;
          }
          else if (_tcsicmp(sSource,_T("allusers"))==0)
          {
            if (_tcsstr(sDest,_T("no"))!=0 ) m_allUsers=false;
          }
          else if (_tcsicmp(sSource, _T("runapp"))==0)
          {
            // add it as an individual file (it should be copied during instalation process)
            CString sFile = parsedLine.GetString(2);
            CString params = parsedLine.GetString(3);
            CString sCondition = parsedLine.GetString(4);
            CSetupFileItem* pItem = new CSetupFileItem(NULL);
            if (pItem == NULL) return FALSE;
            pItem->m_sSourceFileName = sFile;
            pItem->m_sDestinationFileName = ClearCompressionExt(sFile);
            pItem->whereSaved = GetWhereSaved(sFile);
            pItem->m_pCdRom = m_currentCdRom;
            m_arFileList.Add(pItem);
            // add the file into the list of application to be run (and deleted after instalation)

            CSetupFileItem* pItem2 = new CSetupFileItem(NULL);
            pItem2->m_sDestinationFileName = pItem->m_sDestinationFileName;
            pItem2->m_sArguments = params;
            pItem2->m_sCondition = sCondition;
            m_runAppFiles.Add(pItem2);
          }
#if _ENABLE_PBO_PROTECTION
          else if (_tcsicmp(sSource, _T("parentReg"))==0)
          {
            // parent application registry path

            // try to load parent id;
            int id = sDest.Find (':',0);
            if (id > 0) 
            {
              m_sParentAppRegistryRoot = sDest.Mid(0, id);
              m_iParentProjectId = _ttoi(sDest.Mid (id + 1));
            }
            else
              m_sParentAppRegistryRoot = sDest;


          }
          else if (_tcsicmp(sSource, _T("protTestParentSerial"))==0)
          {
            // parent testing serial number for header protection
            PBOHeaderProtection::TestParentSerial = CString2RString(sDest);
          }
          else if (_tcsicmp(sSource, _T("protTestSerial"))==0)
          {
            // parent application registry path
            PBOHeaderProtection::TestSerial = CString2RString(sDest);
          }
#endif //#if _ENABLE_PBO_PROTECTION
          else if (_tcsicmp(sSource, _T("version")) == 0) 
          {
            RequiredVersion ver;
            ver.version = parsedLine.GetString(2);
            ver.textMsg = parsedLine.GetString(3);
            ver.textMsgBody = parsedLine.GetString(4);
            ver.registryKey = parsedLine.GetString(5);
            ver.registryVal = parsedLine.GetString(6);
            ver.fileName = parsedLine.GetString(7);
            m_requiredVersions.Add(ver);
          }
        }
      }
      break;
		case _T ('F'):	// individuální soubor
			{
        if (parsedLine.Size()>=3)
        {
          CString sSource;
          CString sDest;
          CString sCondition;
          CString sDestination;

          int pos=0;
          CString cmd = parsedLine.GetString(pos++);
          int nextch = 1;
          if (cmd.GetLength()>nextch && cmd[nextch]==_T('P'))
          {
            CString name = parsedLine.GetString(pos++);
            CString path = parsedLine.GetString(pos++);
            sDestination = GetSystemPath(name,path);
            ++nextch;
          }
          if (cmd.GetLength()>nextch && cmd[nextch]==_T('C'))
          {
            sSource = parsedLine.GetString(pos++);
            sDest = parsedLine.GetString(pos++);
            sCondition = parsedLine.ConcatenateRest(pos++);
            ++nextch;
          }
          else
          {
            sSource = parsedLine.GetString(pos++);
            sDest = parsedLine.ConcatenateRest(pos++);
          }

					CSetupFileItem* pItem = new CSetupFileItem(&m_arCurrentLogFile);
					if (pItem == NULL)
					{
						return FALSE;
					}
					pItem->m_sSourceFileName = sSource;
					pItem->m_sDestinationFileName = sDest;
          pItem->whereSaved = GetWhereSaved(sSource);
					pItem->m_pCdRom = m_currentCdRom;
          pItem->m_sCondition = sCondition;
          pItem->m_sDifferentDest = sDestination;

					m_arFileList.Add(pItem);
				};
			}; break;
		case _T ('X'):	// individuální soubor bez zapisu do logu pro odinstalator
			{
        if (parsedLine.Size()>=3)
        {
          CString sSource = parsedLine.GetString(1);
          CString sDest = parsedLine.ConcatenateRest(2);
					CSetupFileItem* pItem = new CSetupFileItem(NULL);
					if (pItem == NULL)
					{
						return FALSE;
					}
					pItem->m_sSourceFileName = sSource;
					pItem->m_sDestinationFileName = sDest;
          pItem->whereSaved = GetWhereSaved(sSource);
          pItem->m_pCdRom = m_currentCdRom;
					m_arFileList.Add(pItem);
				}
			}
      break;
		case _T ('S'):	// individuální soubor (shortcut)
			{
        // all possible parameters
        bool source = true;
        bool destination = true;

        bool condition = false;
        bool icon = false;
        bool dll = false;
        bool arguments = false;

        bool desktop = false;
        bool sendTo = false;

        // parse letters in first parameter - definition of additionals parameters
        CString definition = parsedLine.GetString(0);
        for (int i=1; i<definition.GetLength(); ++i)
        {
          char param = definition[i];
          switch (param)
          {
            case _T('C'): condition = true; break;
            case _T('A'): arguments = true; break;
            case _T('D'): desktop = true; break;
            case _T('T'): sendTo = true; break;
            case _T('L'): dll = true; break;
            case _T('I'): icon = true; break;
          }
        }

        CSetupFileItem* pItem = new CSetupFileItem(&m_arCurrentLogFile);
        pItem->m_pCdRom = NULL;

        // now parse rest of the parameters 
        for (int i=1; i < parsedLine.Size();++i)
        {
          CString param = parsedLine.GetString(i);
          if (condition) { pItem->m_sCondition = param; condition = false;} else
          if (source) { pItem->m_sSourceFileName = param; source= false;} else
          if (arguments) { pItem->m_sArguments = param; arguments = false;} else
          if (destination) { pItem->m_sDestinationFileName = param; destination = false;} else
          if (icon) { pItem->m_sIconPath = param; icon = false;} else
          if (dll) 
          { 
            pItem->m_iIconIndex = _tstoi(param);
            dll = false;
          }
        }
        // TODO add support to different destination parameter

        if (sendTo)
          m_arShortcutsSendTo.Add(pItem);
        else if (desktop)
          m_arShortcutsDesktop.Add(pItem);
        else
          m_arShortcuts.Add(pItem);
			}
      break;
		case _T ('P'):	// show picture
			{
        if (parsedLine.Size()>=3)
        {
          CString sPicture = parsedLine.GetString(1);
          CString sLen = parsedLine.ConcatenateRest(2);
					CPictureItem* pItem = new CPictureItem();
					pItem->m_sPictureName = m_sSetupSourceDir;
					pItem->m_sPictureName += sPicture;
          // try to load it from exe resources first
          if (!temporaryFolderActive) PrepareResourceData();
          //if the image was in resource, it is located in temporary directory now
          CString tempDir = GetTempDirectory(m_sApplicationName);
          CString bitmapName = tempDir + CString(_T("\\")) + sPicture;
          pItem->m_hPicture = (HBITMAP)LoadImage
            (
            NULL, bitmapName,
            IMAGE_BITMAP,
            0, 0,
            LR_LOADFROMFILE
            );
          if (pItem->m_hPicture==NULL) //unsuccessful, so try to get the image from resource
          {
            pItem->m_hPicture = (HBITMAP)LoadImage
              (
              NULL, pItem->m_sPictureName,
              IMAGE_BITMAP,
              0, 0,
              LR_LOADFROMFILE
              );
          }
					float len;
					_stscanf(sLen, _T("%f"), &len);
					pItem->m_len = len;
					pItem->m_nFrom = 0;
					m_arPictures.Add(pItem);
				};
			}; break;
		case _T ('I'):		// implicitni cilova cesta pro instalaci
			{
        CString param[2],cmdLine = parsedLine.ConcatenateRest(0);
        int i=1;
        param[0]=parsedLine.GetString(i++);
        if (param[0].CompareNoCase(_T("N"))==0)
          param[1]= parsedLine.GetString(i++)+":"+parsedLine.GetString((i++)+1);
        else param[1]= parsedLine.GetString(i++);

        CString regKeyName = parsedLine.GetString(i++);
        CString regValName = parsedLine.GetString(i++);
        CString regPathExt = parsedLine.GetString(i++);


				if ((regValName!="")&&(regKeyName!=""))
				{
					HKEY key;
					bool mainValSet = false;
					if (RegOpenKey (HKEY_LOCAL_MACHINE, regKeyName, &key) == ERROR_SUCCESS)
					{
						DWORD mainValLength = _MAX_PATH;
						TCHAR mainVal[_MAX_PATH];
						if (RegQueryValueEx (key, regValName, 0, NULL, (BYTE*) (&mainVal[0]), &mainValLength) == ERROR_SUCCESS)
						{
							MakeLongDirStr (m_sSetupPoseidonDir, mainVal);
#if REINFORCEMENTS // do not use reg path if A2 and OA are in same folder
              bool OAisInSameA2Folder = false;
              if (_tcsstr(parsedLine.GetString(2),_T("REINFORCEMENTS")))
              {
                HKEY key2;
                if (RegOpenKey (HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Bohemia Interactive Studio\\ArmA 2 OA"), &key2) == ERROR_SUCCESS)
                {
                  DWORD mainOAValLength = _MAX_PATH;
                  TCHAR mainOAVal[_MAX_PATH];
                  if (RegQueryValueEx (key2, _T("MAIN"), 0, NULL, (BYTE*) (&mainOAVal[0]), &mainOAValLength) == ERROR_SUCCESS)
                  {
                    CString	OAPoseidonDir;
                    MakeLongDirStr (OAPoseidonDir, mainOAVal);
                    if (_tcsicmp(OAPoseidonDir,m_sSetupPoseidonDir)==0) OAisInSameA2Folder = true;
                  }
                }
              }
              mainValSet = !OAisInSameA2Folder;
#else
							mainValSet = true;
#endif
              m_sSetupPoseidonDir += regPathExt;
              MakeShortDirStr (m_sSetupPoseidonDir, m_sSetupPoseidonDir);

						}
						RegCloseKey (key);
					}
					if (mainValSet)	break;
				}
        m_sSetupPoseidonDir = GetSystemPath(param[0],param[1]);
			}
			break;

    case _T ('T'): // name of the file and that will be checked in target directory
      // two parameters H:fileName - check with warning message
      if (parsedLine.Size() == 2)
      {
        m_sTargetFileCheck = parsedLine.ConcatenateRest(1);
        m_bTargetFileRequired = false;
      }
      else
      // three parameters H:R:fileName - check that requires target file to proceed installation
      if (parsedLine.Size() == 3 && parsedLine.GetChar(1) == _T ('R'))
      {
        m_sTargetFileCheck = parsedLine.ConcatenateRest(2);
        m_bTargetFileRequired = true;
      }
      break;

		case _T ('J'):		// nazev podadresare pro instalaci (pouzije se v pripade zmeny cilove cesty uzivatelem)
			m_sInstallationSubdirectory = parsedLine.ConcatenateRest(1);
			break;

		case _T ('G'):		// adresar pro start menu group
			m_sStartMenuGroupDir = parsedLine.ConcatenateRest(1);
			break;

    case _T ('B'):
      m_sCheckFileName = parsedLine.ConcatenateRest(1);
      break;
    case _T ('M'):
      m_sRunAfterUpdate = parsedLine.ConcatenateRest(1);
       break;;

    case _T ('Z'):
      {
        CString value = parsedLine.ConcatenateRest(1);
        if (value.CompareNoCase(_T("true"))==0)
          m_showSetupMode = true;
      }
      break;

		case _T ('U'):		// odinstalator
			{
        if (parsedLine.GetChar(1)==_T ('S'))
				{
          m_delDirsAfterInstallEnd.Add(parsedLine.ConcatenateRest(2));
					break;
				}
        if (parsedLine.GetChar(1)==_T ('F'))
				{
          m_delFilesAfterInstallEnd.Add(parsedLine.ConcatenateRest(2));
					break;
				}
        if (_tcsstr(parsedLine.ConcatenateRest(1),_T("protect"))!=0)
				{
          for (int i = 0; i < m_arCurrentLogFile.GetSize (); ++i)
            AddSetupLogLine(m_arCurrentLogFile[i], 'Q',_T("protect"));
					break;
				}
        if (parsedLine.GetChar(1)==_T ('N'))
				{
					int s = m_arCurrentLogFile.GetSize ();
					if (s > 0)
					{
						m_arCurrentLogFile.RemoveAt (s - 1);
					}
					break;
				}
        //Possible Upgrade from previously installed (preorder) version
        if (parsedLine.GetChar(1)==_T ('U'))
        {
          if (parsedLine.Size()==4)
          {
            m_upgradeTestFile = parsedLine.GetString(2);
            m_uninstalledTestFile = parsedLine.GetString(3);
          }
          break;
        }
        if (parsedLine.GetChar(1)==_T ('P'))
        {
          // define uninstall path setting (for deleting additional files under LocalSettings or MyDocuments)
          // save them and apply in SETUP_STATE_SETUP_LOG 
          m_updateUnistallAfterInstallEnd.Add(parsedLine.ConcatenateRest(2));
          break;
        }

				if (parsedLine.Size()>=5)
        {
          bool addDir = false;
          CString uninst = parsedLine.GetString(2);
          CString appl = parsedLine.GetString(3);
          CString disp = parsedLine.ConcatenateRest(4);

          switch (parsedLine.GetChar(1))
          {
          case _T ('D'):
            addDir=true;
            break;

          default: // '-'
            break;
          }

          int f = uninst.ReverseFind (_T ('.'));
          CString logFileNameStr;
          if (f > 0)
          {
            logFileNameStr = uninst.Left (f) + _T (".log");
          }
          else
          {
            logFileNameStr = uninst + _T (".log");
          }
          m_arCurrentLogFile.Add (logFileNameStr);

          CSetupLogFileItem *uninstall = new CSetupLogFileItem (m_arCurrentLogFile, logFileNameStr, uninst, appl, disp, addDir);
          if (NULL != uninstall)
          {
            m_arSetupLogFiles.Add (uninstall);
          }
        }
			}
			break;

		case _T ('A'):		// nazev aplikace
			{
				m_sApplicationName = parsedLine.ConcatenateRest(1);
				for (int i = 0; i < m_arCurrentLogFile.GetSize (); ++i)
				{
					m_arApplLogFileName.Add (m_arCurrentLogFile[i]);
				}
				if (NULL != m_pPosSetupApp)
				{
					m_pPosSetupApp->m_sAppName.Replace (_T("%0"), m_sApplicationName);
					m_pPosSetupApp->m_pszAppName = m_pPosSetupApp->m_sAppName;
				}
			}
			break;

		case _T ('R'):		// cesta ke korenovemu adresari pro zapis do registru
			m_sRegistryRoot.Add(parsedLine.ConcatenateRest(1));
			break;

		case _T ('W'):		// akce pro zaverecny dialog
		case _T ('O'):
		case _T ('E'):
			{
				if (m_arActions.GetSize () < 10)
				{
					CString *action = new CString ((LPCTSTR) wholeLine);
					if (NULL != action)
					{
						m_arActions.Add (action);
					}
				}
			}
			break;

		case _T ('K'):		// kontrola CD klice zapsaneho v registrech
      {
        // K2: prefix for new algorithm for CD keys (ArmA 2)
        TCHAR modifier = parsedLine.GetChar(0, 1);
			  m_cdKeyOptions.init (parsedLine.ConcatenateRest(1), modifier == _T('2'));
      }
			break;

		case _T ('C'):		// definice CD-ROM z ktereho se bude instalovat
			{
        if (parsedLine.Size()<4) return FALSE;
        m_currentCdRom = new CCdRom (parsedLine.GetString(1), 
					parsedLine.GetString(2),
					parsedLine.ConcatenateRest(3));
				if (m_currentCdRom == NULL)
				{
					return FALSE;
				}
				m_cds.Add (m_currentCdRom);
			}
			break;

		case _T ('L'):		// delka vsech instalovanych souboru
			{
				TCHAR *end;
        const TCHAR *start = parsedLine.GetString(1);
				m_TotalCopySize = _tcstoi64 (start, &end, 0);
				if (end == start)
				{
					return FALSE;
				}
			}
			break;
    default:
      {
        if (m_debugMode)
        {
          CString warnInfo; warnInfo.Format(_T("Warning: The following setup.dat line was not recognized as command\n\n%s"), parsedLine.ConcatenateRest(0));
          SetupMessageBox(warnInfo);
        }
      }
      break;
		};
	}
  // us default register if not set in setup.dat 
  if (m_sRegistryRoot.GetCount()==0) 
    m_sRegistryRoot.Add(m_sRegistryRootDefault);


	m_currentCdRom = NULL;

  int productID = m_cdKeyOptions.m_newKeys ? m_cdKeyOptions.m_productID : -1;

	if (m_cdKeyOptions.action == CKeyOptions::readFromRegistry && 
		  m_cdKeyOptions.m_cdKeyIsInRegistry && 
		(!CheckCDKEYfromRegistry(productID, m_cdKeyOptions.m_cdKeyFromRegistry, 
		            m_cdKeyOptions.m_publicKey, 
					m_cdKeyOptions.m_checkOffset,
					CString2RString(m_cdKeyOptions.m_checkWith))))
	{
		SetupMessageBox(SetupLocalizeString (_T("SETUP_STR_BAD_CDKEY")),MB_OK|MB_ICONEXCLAMATION);
		return FALSE;
	}

  if (m_cdKeyOptions.readFromCommandLine && 
    (!CheckCDKEY(productID, m_cdKeyOptions.m_cdKeyFromCommandLine, 
    m_cdKeyOptions.m_publicKey, 
    m_cdKeyOptions.m_checkOffset,
    CString2RString(m_cdKeyOptions.m_checkWith)))
    )
  {
    SetupMessageBox(SetupLocalizeString (_T("SETUP_STR_BAD_CDKEY")),MB_OK|MB_ICONEXCLAMATION);
    return FALSE;
  }

  // CWA setup 
  if (m_cdKeyOptions.action == CKeyOptions::readFromOFPReg && 
    (!m_cdKeyOptions.m_cdKeyIsInRegistry ||
    !CheckCDKEYfromRegistry(productID, m_cdKeyOptions.m_cdKeyFromRegistry, 
    m_cdKeyOptions.m_publicKey, 
    m_cdKeyOptions.m_checkOffset,
    CString2RString(m_cdKeyOptions.m_checkWith))))
  {
    m_cdKeyOptions.m_cdKeyIsInRegistry = false;
  }
	
	CString crcFilename = m_sSetupSourceDir+SETUP_CRC_FILE_NAME;
	
	// nacte CRC pro vsechny zname soubory

  if (!temporaryFolderActive) PrepareResourceData();
  CString tempDir = GetTempDirectory(m_sApplicationName);
  CString crcTmpFileLocation = tempDir + CString(_T("\\")) + SETUP_CRC_FILE_NAME;
  FILE *f = _tfopen(crcTmpFileLocation, _T("r"));
  if (!f)
  { //crc file can be also saved in temporary dir (from setup resources)
    f = _tfopen(crcFilename,_T("r"));
  }
	if (f)
	{
		for(;;)
		{
			char line[1024];
			const char *ok = fgets(line,sizeof(line),f);
			if (!ok) break;
			if (*line && line[strlen(line)-1]=='\n') line[strlen(line)-1]=0;
			// search for file in object list
			const char *sp = strchr(line,' ');
			if (!sp) continue;
			sp++;
			CCrcFile *crcFile = new CCrcFile (CString (line, sp - 1 - line), 
				strtoul (sp, NULL, 16));
			if (crcFile == NULL)
			{
				return FALSE;
			}
			m_arCrcFiles.Add (crcFile);
		}
		fclose(f);
	}

	// calculate total length of pictures
	double sum = 0;
	for (int i=0; i<m_arPictures.GetSize(); i++)
	{
		CPictureItem* pItem = (CPictureItem*)(m_arPictures[i]);
		sum += pItem->m_len;
	}
	// calculate show begins for pictures
	double coef = (double)m_TotalCopySize / sum;
	double actual = 0;
	for (int i=0; i<m_arPictures.GetSize(); i++)
	{
		CPictureItem* pItem = (CPictureItem*)(m_arPictures[i]);
		pItem->m_nFrom = (__int64)(actual * coef + 0.5);
		actual += pItem->m_len;
	}
	return (m_TotalCopySize != 0);
};

bool CSetupObject::FindCRC(const LPCTSTR name, unsigned int &crc)
{
	for (int j = 0; j < m_arCrcFiles.GetSize (); ++j)
	{
		if (_tcsicmp(ClearCompressionExt(name), ClearCompressionExt(m_arCrcFiles[j]->_name)) == 0)
		{
			crc = m_arCrcFiles[j]->_crc;
			return true;
		}
	}
  return false;
}

// Undo setup process
#include "io.h"
#include "direct.h"

/* DoDeleteFile */
void DoDeleteFile(LPCSTR pFile)
{
	if (_access(pFile,00)==0)
	{                    
		OFSTRUCT ofs;
		OpenFile(pFile,&ofs,OF_DELETE);
	}
}

// check folder for empty, need to check before rmdir, because it delete non-empty softlink or junction 
bool isDirEmpty(LPCSTR name)
{
  if (!name || *name == 0) return true;

  char buffer[256];
  sprintf_s(buffer, 256, "%s\\*.*", name);

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
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
  return true;
}

/* DoDeleteDir */
void DoDeleteDir(LPCSTR pDir)
{
	SetCurrentDirectory(_T("C:\\"));
  // delete only empty folder, rmdir doesn't delete empty folder but it delete non-empty softlink or junction 
  if (isDirEmpty(pDir)) rmdir(pDir);
};

/* DoDeleteKey */
void DoDeleteKey(HKEY root, LPCSTR buff)
{
	HKEY hKey;
	if (RegOpenKeyA(root,buff,&hKey)==ERROR_SUCCESS)
	{
		DWORD nSubKeys;
		if (RegQueryInfoKey(hKey,NULL,NULL,NULL,&nSubKeys,
										NULL,NULL,NULL,NULL,NULL,NULL,NULL)==ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			if (nSubKeys == 0)
				RegDeleteKeyA(root,buff);
		}
	};
};

#if USE_RECURSIVE_REGISTRY_DELETE
// we are afraid to allow recursive delete, we use careful version deleting only key with single Expansion subkey, and this recursively instead
DWORD DoDeleteTree(HKEY hStartKey , LPCTSTR pKeyName )
{
  const int MaxKeyLength = 1024;

  DWORD   dwRtn, dwSubKeyLength;
  TCHAR   szSubKey[MaxKeyLength];
  HKEY    hKey;

  // Do not allow NULL or empty key name
  if ( pKeyName &&  lstrlen(pKeyName))
  {
    if( (dwRtn=RegOpenKeyEx(hStartKey,pKeyName,
      0, KEY_ENUMERATE_SUB_KEYS | DELETE, &hKey )) == ERROR_SUCCESS)
    {
      while (dwRtn == ERROR_SUCCESS )
      {
        dwSubKeyLength = MaxKeyLength;
        dwRtn=RegEnumKeyEx(
          hKey,
          0,       // always index zero
          szSubKey,
          &dwSubKeyLength,
          NULL,
          NULL,
          NULL,
          NULL
          );

        if(dwRtn == ERROR_NO_MORE_ITEMS)
        {
          dwRtn = RegDeleteKey(hStartKey, pKeyName);
          break;
        }
        else if(dwRtn == ERROR_SUCCESS)
          dwRtn=DoDeleteTree(hKey, szSubKey);
      }
      RegCloseKey(hKey);
      // Do not save return code because error
      // has already occurred
    }
  }
  else
    dwRtn = ERROR_BADKEY;

  return dwRtn;
}
#else 

// we are afraid to allow recursive delete, we use careful version deleting only key with single Expansion subkey, and this recursively instead
DWORD DoDeleteTree(HKEY hStartKey , LPCTSTR pKeyName, bool insideExpansions=false )
{
  const int MaxKeyLength = 1024;

  DWORD   dwRtn, dwSubKeyLength;
  TCHAR   szSubKey[MaxKeyLength];
  HKEY    hKey;

  // Do not allow NULL or empty key name
  if ( pKeyName &&  lstrlen(pKeyName))
  {
    if( (dwRtn=RegOpenKeyEx(hStartKey,pKeyName,
      0, KEY_ENUMERATE_SUB_KEYS | DELETE, &hKey )) == ERROR_SUCCESS)
    {
      while (dwRtn == ERROR_SUCCESS )
      {
        dwSubKeyLength = MaxKeyLength;
        dwRtn=RegEnumKeyEx(
          hKey,
          0,       // always index zero
          szSubKey,
          &dwSubKeyLength,
          NULL,
          NULL,
          NULL,
          NULL
          );

        if(dwRtn == ERROR_SUCCESS)
        {
          if (_tcsicmp(szSubKey, _T("expansions"))==0 )
          {
            dwRtn=DoDeleteTree(hKey, szSubKey, true);
          }
          else if (insideExpansions)
          {
            dwRtn=DoDeleteTree(hKey, szSubKey, insideExpansions);
          }
          else dwRtn = ERROR_DIR_NOT_EMPTY;
        }
        else if(dwRtn == ERROR_NO_MORE_ITEMS)
        {
          dwRtn = RegDeleteKey(hStartKey, pKeyName);
          break;
        }
      }
      RegCloseKey(hKey);
      // Do not save return code because error
      // has already occurred
    }
  }
  else
    dwRtn = ERROR_BADKEY;

  return dwRtn;
}
#endif

typedef HRESULT (CALLBACK *RemoveFromGameExplorer)(GUID *);
typedef HRESULT (CALLBACK *RemoveTasks)(GUID *); 

void DoUnregisterFromGameExplorer(LPCSTR buff)
{
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

/* DoUnInstallCmd */
void DoUnInstallCmd(LPCTSTR pCmd)
{
  if (lstrlen(pCmd)<3)
    return;
  if (pCmd[1] != _T(':'))
    return;

  // parametr
  TCHAR buff[1000];
  lstrcpy(buff, pCmd + 2);

	switch(pCmd[0])
	{
	case _T('F'):
	case _T('f'):	// delete file
		DoDeleteFile(LPCTSTR2RString(buff));
		break;
	case _T('D'):
	case _T('d'):	// delete dir
		DoDeleteDir(LPCTSTR2RString(buff));
		break;
	case _T('K'):
	case _T('k'):	// delete key LOCAL_MACHINE
		DoDeleteTree(HKEY_LOCAL_MACHINE, buff);
		break;
  case _T('C'):
  case _T('c'):	// delete key CLASSES_ROOT
    DoDeleteTree(HKEY_CLASSES_ROOT, buff);
    break;
  case _T('G'):
  case _T('g'):	// unregister from the Game Explorer
    DoUnregisterFromGameExplorer(LPCTSTR2RString(buff));
    break;
  case _T('A'):
  case _T('a'):	// unregister extended file association
    DoDeleteFileTypeEx(buff);
    break;
	};
};

extern bool FileExists(CString &path);

void CSetupObject::UndoSetup()
{
	for (int s = 0; s < m_arSetupLogFiles.GetSize (); ++s)
	{
		CSetupLogFileItem *pItem = (CSetupLogFileItem*) m_arSetupLogFiles[s];
		if (NULL != pItem)
		{
			for (int i = pItem->m_arSetupLog.GetSize() - 1; i >= 0; i--)
			{
				DoUnInstallCmd(pItem->m_arSetupLog[i]);
			}
		}
	}

  for (int i=0; i<m_runAppFiles.GetSize(); i++)
  {
    CSetupFileItem* pItem = (CSetupFileItem*)(m_runAppFiles[i]);
    CString appName = pItem->m_sDestinationFileName;
    CString dir; MakeLongDirStr (dir, m_sSetupPoseidonDir);

    if (!FileExists(dir + appName)) continue;
    for (int i=0;i<10;i++)
    {
      if(remove(CString2RString(dir + appName))==0) break;
      Sleep(100);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
// CResultOK dialog

class CResultOK : public CDialog
{
	CSetupObject *m_pSetup;

public:
	const static int m_arButtonIds[];

// Construction
public:
	CResultOK(CSetupObject *setupObj, CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CResultOK)
	enum { IDD = IDD_RESULT_OK };
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CResultOK)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CResultOK)
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

const int CResultOK::m_arButtonIds[] =
{
	IDC_FINACTION0,
	IDC_FINACTION1,
	IDC_FINACTION2,
	IDC_FINACTION3,
	IDC_FINACTION4,
	IDC_FINACTION5,
	IDC_FINACTION6,
	IDC_FINACTION7,
	IDC_FINACTION8,
	IDC_FINACTION9
};

CResultOK::CResultOK(CSetupObject *setupObj, CWnd* pParent /*=NULL*/)
	: CDialog(CResultOK::IDD, pParent),
	m_pSetup (setupObj)
{
	//{{AFX_DATA_INIT(CResultOK)
	//}}AFX_DATA_INIT
}

BOOL CResultOK::OnInitDialog() 
{
	CDialog::OnInitDialog();

	for (int i = 0; i < 10; ++i)
	{
		CButton *but = (CButton*) GetDlgItem (m_arButtonIds[i]);
		if (NULL != but)
		{
			but->SetCheck (0);
			but->EnableWindow (FALSE);
			but->ShowWindow (SW_HIDE);
		}
	}

	CButton *lastBut = NULL;
	for (int i = 0; i < m_pSetup->m_arActions.GetSize (); ++i)
	{
		CString *action = (CString *) (m_pSetup->m_arActions[i]);
		if (NULL != action)
		{
			lastBut = (CButton*) GetDlgItem (m_arButtonIds[i]);
			if (NULL != lastBut)
			{
				int f = action->Find (_T ('|'), 6);
				if (f > 0)
				{
					if ((*action)[2] == _T ('+'))
					{
						lastBut->SetCheck (1);
					}
          //lastBut->SetWindowText (((LPCTSTR) (*action)) + f + 1);
          SetupSetWindowText(lastBut, ((LPCTSTR) (*action)) + f + 1);
					lastBut->ShowWindow (SW_SHOW);
					lastBut->EnableWindow (TRUE);
				}
			}
		}
	}

	m_pSetup->ApplyApplicationNameAndStringTable (this);

#define SPACE 7

	RECT r;
	RECT wr;
	CWnd *w;
	int y = 70;
	if (lastBut == NULL)
	{
		w = GetDlgItem (IDC_STATICMESSAGE);
		if (w != NULL)
		{
			w->GetWindowRect (&r);
			ScreenToClient (&r);
			y = r.bottom + SPACE;
		}
	}
	else
	{
		lastBut->GetWindowRect (&r);
		ScreenToClient (&r);
		y = r.bottom + SPACE;
	}

	w = GetDlgItem (IDOK);
	if (w != NULL)
	{
		w->GetWindowRect (&r);
		r.bottom = r.bottom - r.top + y;
		r.top = y;
		w->MoveWindow (&r);
		y = r.bottom + SPACE;
	}

	GetWindowRect (&wr);
	GetClientRect (&r);
	r.bottom = r.top + y;
	CalcWindowRect (&r);
	wr.bottom = wr.top + r.bottom - r.top;
	wr.right = wr.left + r.right - r.left;
	MoveWindow (&wr);

#undef SPACE

	return TRUE;
}

void CResultOK::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CResultOK)
	//}}AFX_DATA_MAP

	if (pDX->m_bSaveAndValidate)
	{
		for (int i = 0; i < m_pSetup->m_arActions.GetSize (); ++i)
		{
			CButton *but = (CButton*) GetDlgItem (m_arButtonIds[i]);
			if (NULL != but)
			{
				m_pSetup->m_arActionChecks[i] = but->GetCheck ();
			}
		}
	}
}

BEGIN_MESSAGE_MAP(CResultOK, CDialog)
	//{{AFX_MSG_MAP(CResultOK)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CResultOK message handlers

BOOL CSetupObject::DoResultOK()
{
	CResultOK	dlg (this);
	dlg.DoModal();

	return TRUE;
}

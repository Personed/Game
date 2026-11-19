// OFPPatch.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <Es/essencepch.hpp>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "resource.h"

#pragma warning(disable: 4996)

#include <Es/Files/filenames.hpp>
#include <Es/Framework/debugLog.hpp>
#include <Es/Containers/array.hpp>

#include <El/ParamFile/paramFile.hpp>
#include <El/CRC/crc.hpp>

#include "consoleWindow.hpp"
extern const char *AppName;
extern const char *PatchVersionTo;

#include "fileMap.hpp"

#include "serial.hpp"

#if _ENABLE_PBO_PROTECTION
#include <El/Encryption/pboheaderprotection.h>
#endif

#ifndef _SUPER_RELEASE
#include "signatures.hpp"
#endif

#include <Commctrl.h>
#include <shlobj.h>

#include <Tlhelp32.h>

//global variable
AutoArray<RString> GUninstalLog;  //to know what new files were patched

class ConsoleWindowProgress: public ConsoleWindow, public RefCount
{
  typedef ConsoleWindow base;

  public:
  ConsoleWindowProgress();
  ~ConsoleWindowProgress();

  virtual int DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
  void StopAnimation();
};

void ConsoleWindowProgress::StopAnimation()
{
  HWND aviControl = GetDlgItem(GetHWnd(),IDC_PROGRESS_AVI);
  Animate_Close(aviControl);
  ::ShowWindow(aviControl, SW_HIDE);
}

ConsoleWindowProgress::ConsoleWindowProgress()
:base()
{
}
ConsoleWindowProgress::~ConsoleWindowProgress()
{}

int ConsoleWindowProgress::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  int ret = base::DlgProc(hDlg,message,wParam,lParam);
  switch (message)
  {
    case WM_INITDIALOG:
    {
      HWND aviControl = GetDlgItem(hDlg,IDC_PROGRESS_AVI);
      Animate_OpenEx(aviControl,NULL,MAKEINTRESOURCE(IDR_COPYFILE));
    }
  }
  return ret;
}

//#pragma comment(lib,"comctl32.lib")

// Macrovision CD Protection
#include "cdapfn.h"
#include "patch.hpp"

#define USE_RESOURCES	1

#define PARSE_RESOURCE_OURSELVES 0

//#define RES_TYPE "BINARY"
#define RES_TYPE MAKEINTRESOURCE(RT_RCDATA)

#pragma comment(lib,"version")

// Macrovision CD Protection
bool __cdecl FileExist(const char *name);
CDAPFN_DECLARE_GLOBAL(FileExist, CDAPFN_OVERHEAD_L4, CDAPFN_CONSTRAINT_NONE);

bool __cdecl FileExist(const char *name)
{
  // check normal file existence
  HANDLE check=::CreateFile
  (
    name,GENERIC_READ,FILE_SHARE_READ,
    NULL,OPEN_EXISTING,0,NULL
  );
  if( check==INVALID_HANDLE_VALUE ) return false;
  ::CloseHandle(check);

  CDAPFN_ENDMARK(FileExist);

  return true;
}

void ErrorMessage(HINSTANCE hInstance, const char *buffer, int icon = MB_ICONERROR)
{
  char caption[256];
  LoadAppString(hInstance, IDS_CAPTION, caption, sizeof(caption));
  LogF("%s",buffer);
  MessageBox(NULL, buffer, caption, MB_OK | icon);
}


void DisplayErrorMessage(RString moreInfo)
{
  LPVOID lpMsgBuf;
  FormatMessage( 
      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
      FORMAT_MESSAGE_FROM_SYSTEM | 
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
      (LPTSTR) &lpMsgBuf,
      0,
      NULL 
  );
  // Display the string.
  RString errMsg = Format("%s %s",(LPCTSTR)lpMsgBuf, cc_cast(moreInfo));
  MessageBox( NULL, cc_cast(errMsg), "Error", MB_OK | MB_ICONINFORMATION );
  #if _DEBUG
    OutputDebugString((LPCTSTR)lpMsgBuf);
  #endif
  // Free the buffer.
  LocalFree( lpMsgBuf );
}

#if PARSE_RESOURCE_OURSELVES

// Given a section name, look it up in the section table and return a
// pointer to the start of its raw data area.
LPVOID GetSectionPtr(PSTR name, PIMAGE_NT_HEADERS pNTHeader, DWORD imageBase)
{
  PIMAGE_SECTION_HEADER section;
  unsigned i;
  
  section = (PIMAGE_SECTION_HEADER)(pNTHeader+1);
  
  for ( i=0; i < pNTHeader->FileHeader.NumberOfSections; i++, section++ )
  {
    if ( strnicmp((const char *)section->Name, name, IMAGE_SIZEOF_SHORT_NAME) == 0 )
      return (LPVOID)(section->PointerToRawData + imageBase);
  }
  
  return 0;
}

// If a resource entry has a string name (rather than an ID), go find
// the string and convert it from unicode to ascii.
void GetResourceNameFromId
(
  DWORD id, DWORD resourceBase, PSTR buffer, UINT cBytes
)
{
  PIMAGE_RESOURCE_DIR_STRING_U prdsu;

  // If it's a regular ID, just format it.
  if ( !(id & IMAGE_RESOURCE_NAME_IS_STRING) )
  {
    wsprintf(buffer, "%X", id);
    return;
  }
  
  id &= 0x7FFFFFFF;
  prdsu = (PIMAGE_RESOURCE_DIR_STRING_U)(resourceBase + id);

  // prdsu->Length is the number of unicode characters
  WideCharToMultiByte(CP_ACP, 0, prdsu->NameString, prdsu->Length,
            buffer, cBytes,	0, 0);
  buffer[ min(cBytes-1,prdsu->Length) ] = 0;	// Null terminate it!!!
}

PIMAGE_RESOURCE_DIRECTORY_ENTRY FindResourceEntry(PIMAGE_RESOURCE_DIRECTORY resDir, DWORD id)
{
  PIMAGE_RESOURCE_DIRECTORY_ENTRY resDirEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(resDir + 1);
  int i;
  // skip named entries
  for (i=0; i<resDir->NumberOfNamedEntries; i++, resDirEntry++);
  for (i=0; i<resDir->NumberOfIdEntries; i++, resDirEntry++)
  {
    if (resDirEntry->Name == id) return resDirEntry;
  }
  return NULL;
}

PIMAGE_RESOURCE_DIRECTORY_ENTRY FindResourceEntry(PIMAGE_RESOURCE_DIRECTORY resDir, DWORD base, const char *name)
{
  PIMAGE_RESOURCE_DIRECTORY_ENTRY resDirEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(resDir + 1);
  int i;
  for (i=0; i<resDir->NumberOfNamedEntries; i++, resDirEntry++)
  {
    char buffer[128];
    if (resDirEntry->NameIsString)
    {
      GetResourceNameFromId(resDirEntry->Name, base, buffer, sizeof(buffer));
      if (strcmpi(buffer, name) == 0)
      {
        return resDirEntry;
      }
    }
  }
  // for (i=0; i<resDir->NumberOfIdEntries; i++, resDirEntry++);
  return NULL;
}

PIMAGE_RESOURCE_DIRECTORY GetResourceDirectory
(
  HINSTANCE hInstance, void *&rbase,
  PIMAGE_NT_HEADERS &ntHeader
)
{
  DWORD base = (DWORD)rbase;
  PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
  {
    ErrorMessage(hInstance, "Unrecognized file format");
    return NULL;
  }

  PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);
  if (IsBadReadPtr(pNTHeader, sizeof(IMAGE_NT_HEADERS)) || pNTHeader->Signature != IMAGE_NT_SIGNATURE)
  {
    ErrorMessage(hInstance, "Unhandled EXE type, or invalid .EXE");
    return NULL;
  }
  ntHeader = pNTHeader;

  PIMAGE_RESOURCE_DIRECTORY resDir = (PIMAGE_RESOURCE_DIRECTORY)GetSectionPtr(".rsrc", pNTHeader, base);
  if (!resDir)
  {
    ErrorMessage(hInstance, "Resources not found.");
    return NULL;
  }

  base = (DWORD) resDir;
  rbase = resDir;

  PIMAGE_RESOURCE_DIRECTORY_ENTRY resDirEntry = FindResourceEntry(resDir, (int)RT_RCDATA);
  if (!resDirEntry)
  {
    ErrorMessage(hInstance, "DATA Resources not found.");
    return NULL;
  }
  
  if ((resDirEntry->OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY) == 0)
  {
    ErrorMessage(hInstance, "DATA Resources is not directory.");
    return NULL;
  }

  return (PIMAGE_RESOURCE_DIRECTORY)(base + (resDirEntry->OffsetToData & 0x7FFFFFFF));
}

//
// Given an RVA, look up the section header that encloses it and return a
// pointer to its IMAGE_SECTION_HEADER
//
PIMAGE_SECTION_HEADER GetEnclosingSectionHeader(DWORD rva,
                                                PIMAGE_NT_HEADERS pNTHeader)
{
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(pNTHeader);
    unsigned i;
    
    for ( i=0; i < pNTHeader->FileHeader.NumberOfSections; i++, section++ )
    {
        // Is the RVA within this section?
        if ( (rva >= section->VirtualAddress) && 
             (rva < (section->VirtualAddress + section->Misc.VirtualSize)))
            return section;
    }
    
    return 0;
}

LPVOID GetPtrFromRVA( DWORD rva, PIMAGE_NT_HEADERS pNTHeader, DWORD imageBase )
{
  PIMAGE_SECTION_HEADER pSectionHdr;
  INT delta;
    
  pSectionHdr = GetEnclosingSectionHeader( rva, pNTHeader );
  if ( !pSectionHdr )
    return 0;

  delta = (INT)(pSectionHdr->VirtualAddress-pSectionHdr->PointerToRawData);
  return (PVOID) ( imageBase + rva - delta );
}

// Each Resource Data Entry has the following format:

struct ResData
{
  DWORD _DATARVA;
  DWORD _SIZE;
  DWORD _CODEPAGE;
  DWORD _RESERVED;
};

struct ResContext
{
  PIMAGE_RESOURCE_DIRECTORY resDir;
  DWORD base;
  DWORD ibase;
  PIMAGE_NT_HEADERS ntHeader;

};

static void *FindAndLoadResource(int &size, const ResContext *ctx, const char *filename)
{
  PIMAGE_RESOURCE_DIRECTORY_ENTRY entry = FindResourceEntry(ctx->resDir, ctx->base, filename);
  if (!entry)
  {
    //char buffer[256]; sprintf(buffer, "resource = %s", filename);
    //MessageBox( NULL, buffer, "Error in FindResource", MB_OK | MB_ICONINFORMATION);
    return NULL;
  }

  // searched resource is first one (and only one) in directory
  if (entry->DataIsDirectory)
  {
    char name[256];
    GetResourceNameFromId(entry->Name,ctx->base,name,sizeof(name));
    //char *name = (char *)(base+entry->NameOffset);

    PIMAGE_RESOURCE_DIRECTORY rd = (PIMAGE_RESOURCE_DIRECTORY)(ctx->base + (entry->OffsetToData&0x7fffffff));
    entry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(rd+1);
  }

  ResData *rd = (ResData *)(ctx->base + (entry->OffsetToData&0x7fffffff));

  void *ptr = GetPtrFromRVA(rd->_DATARVA,ctx->ntHeader,ctx->ibase);
  //void *ptr = (void *)(rd->_DATARVA - 0x2000  + ctx->ibase); 
  size = rd->_SIZE;
  return ptr;

}

DWORD ResourceSize(const char *source, const ResContext *ctx)
{
  int size; 
  void *ptr = FindAndLoadResource(size,ctx,source);
  if (!ptr) return 0;
  return size;
}

#define RES_CONTEXT const ResContext *

#else


struct ExeContext
{
  HMODULE hMod;
};

void *FindAndLoadResource(int &size, const ExeContext *ctx, const char *source)
{
  // ctx is here only to make function signature similar to FindAndLoadResource
  // in PARSE_RESOURCE_OURSELVES branch
  char upName[512];
  strcpy(upName,source);
  strupr(upName);
  HRSRC hRsc = FindResource(ctx->hMod, upName, RES_TYPE);
  if (!hRsc)
  {
    return NULL;
  }
  HGLOBAL hMem = LoadResource(ctx->hMod, hRsc);
  size = SizeofResource(ctx->hMod, hRsc);
  return LockResource(hMem);
}


DWORD ResourceSize(const char *source, const ExeContext *ctx)
{
  char upName[512];
  strcpy(upName,source);
  strupr(upName);
  HRSRC hRsc = FindResource(ctx->hMod, upName, RES_TYPE);
  if (!hRsc)
  {
    DisplayErrorMessage(Format("resource: %s",upName));
    return false;
  }
  return SizeofResource(ctx->hMod, hRsc);
}

#define RES_CONTEXT const ExeContext *

#endif

//! create path to given directory
void CreatePath(const char *path)
{
  // string will be changed temporary
  char name[256];
  strcpy(name, path);
  char *end = name;
  while (end = strchr(end, '\\'))
  {
    *end = 0;
    mkdir(name);
    *end = '\\';
    end++;
  }
}


bool ResourceToFile(const char *source, const char *destination, RES_CONTEXT ctx)
{
  int size;
  void *ptr = FindAndLoadResource(size,ctx,source);
  if (!ptr)
  {
    DisplayErrorMessage(Format("resource: %s",source));
    return false;
  }

  CreatePath(destination);
  chmod(destination,S_IREAD | S_IWRITE);
  unlink(destination);
  int f = open
  (
    destination,O_CREAT|O_TRUNC|O_WRONLY|O_BINARY,S_IREAD | S_IWRITE
  );
  if (f<0) return false;

  bool ok = true;
  while (size>0 && ok)
  {
    int step = size;
    if (step>1024*1024) step = 1024*1024;

    int wr = write(f, ptr, step);
    if (wr!=step) ok = false;
    if (!ok)
    {
      #if 1 //_DEBUG
        char buffer[256];
        sprintf(buffer, "errno = %d (0x%x), %s", errno, errno, destination);
        MessageBox( NULL, buffer, "Error in write", MB_OK | MB_ICONINFORMATION);
      #endif
    }
    else
    {
      size -= step;
      ptr = (char *)ptr + step;
    }
  }

  if (close(f)<0)
  {
    ok = false;
    #if _DEBUG
      char buffer[256];
      sprintf(buffer, "errno = %d (0x%x)", errno, errno);
      MessageBox( NULL, buffer, "Error in close", MB_OK | MB_ICONINFORMATION);
    #endif
  }
  return ok;
}

static const char *OpenResourceText(const char *txt,RES_CONTEXT ctx)
{
  int size;
  void *ptr = FindAndLoadResource(size,ctx,txt);
  if (size<=0 || !ptr ) return NULL;
  char *cptr = new char[size+1];
  memcpy(cptr,ptr,size);
  cptr[size] = 0;
  return cptr;
}

struct EnumApplyContext 
{
  int verMajor,verMinor; // version number (source)
  int verMajorPatch,verMinorPatch; // version number (target)
  char version[512]; // version path
  char dir[512]; // destination path
  char errorFile[512]; // when error, say where
  ConsoleWindow *console;
  RES_CONTEXT ctx;

  bool found;
  bool error;

  AutoArray<RString> files; // result for retrieving of file list 
  RString distribution; // current distribution
  RString targetDir; // target subdirectory to store patched files in order to leave original files untouched
};

enum RetryResult
{
  RRAbort,
  RRRetry,
  RRIgnore
};

static RetryResult AskRetry
(
  HINSTANCE inst, HWND wnd,
  bool enableRetry,UINT ids, ...
)
{
  char caption[256];
  LoadAppString(inst, IDS_CAPTION, caption, sizeof(caption));
  char buf[2048];
  char format[1024];
  va_list arglist;
  va_start( arglist, ids );
  LoadAppString(inst,ids,format,sizeof(format));
  _vsnprintf(buf,sizeof(buf),format,arglist);
  va_end( arglist );
  int flags = MB_ABORTRETRYIGNORE;
  if (enableRetry) flags |= MB_DEFBUTTON2;
  else flags |= MB_DEFBUTTON1;
  Repeat:
  int mbr = MessageBox(wnd,buf,caption,MB_ICONEXCLAMATION|flags);
  if (mbr==IDABORT)
  {
    return RRAbort;
  }
  else if (mbr==IDIGNORE)
  {
    return RRIgnore;
  }
  else //if (mbr==IDRETRY)
  {
    if (enableRetry) return RRRetry;
    goto Repeat;
  }
}

enum CheckVersionResult
{
  CVRError,
  CVROK,
  CVRRetry
};

struct CheckVersionInfo
{
  RString name;
  RString path;
  int distribution;
  AutoArray<unsigned int> crc;
  bool addon;
};
TypeIsMovable(CheckVersionInfo)

/// if we should write file that the crc is tested for, ONLY FOR TESTING PURPOSES
#define _WRITE_FILE_CRC 0
QOFStream outCRCFile;

#if _ENABLE_PBO_PROTECTION
// computes crc from given range in stream for protected range
bool ComputeCRCRangeProtected(const RString& fileName, QIStream& in, CRCCalculator& calc, QFileSize startOffset, QFileSize size)
{
  /// we have to decrypt stream by serial numbers key and encrypt it again by setup key
  /// get serial numbers
  PBOHeaderProtection::TByteBuffer serial1;
  PBOHeaderProtection::TByteBuffer serial2;
  if (!PBOHeaderProtection::CreateProtectionSerialsFromPBO(PBOHeaderProtection::AppRegistryPath, fileName, serial1, serial2))
  {
    RString errorMessage = Format("Failed to get serial numbers for header decryption.");
    printf(cc_cast(errorMessage));
    ErrF(cc_cast(errorMessage));
    return false;
  }

  /// create protection that we will use to decrypt headers
  SRef<IFilebankEncryption> decryptClass(PBOHeaderProtection::CreateSerialHeaderProtection(serial1, serial2));
  if (decryptClass.IsNull())
  {
    RString errorMessage = "Failed to create class for PBO header decryption.";
    printf(cc_cast(errorMessage));
    ErrF(cc_cast(errorMessage));
    return false;
  }

  // create encryption class
  SRef<IFilebankEncryption> encryptClass(PBOHeaderProtection::CreateSetupHeaderProtection());
  if (encryptClass.IsNull())
  {
    RString errorMessage = Format("Failed to create class for PBO header encryption.");
    printf(cc_cast(errorMessage));
    ErrF(cc_cast(errorMessage));
    return false;
  }

  // remember current pos
  QFileSize oldPos = in.tellg();

  // seek to start pos
  in.seekg(startOffset);
  if (in.fail())
  {
    in.seekg(oldPos);
    return false;
  }

  QOStrStream newStream;
  /// transform stream
  PBOHeaderProtection::TransformResult result = PBOHeaderProtection::TransformStream(in, newStream, decryptClass, encryptClass);

  if (result != PBOHeaderProtection::TROk)
  {
    RString resError = PBOHeaderProtection::TransformResultToString(result);
    RString errorMessage = Format("Failed to decrypt and encrypt stream - \"%s\"", cc_cast(resError));
    printf(cc_cast(errorMessage));
    ErrF(cc_cast(errorMessage));
    in.seekg(oldPos);
    return false;
  }

  calc.Add(newStream.str(), newStream.pcount());    
#if _WRITE_FILE_CRC
  outCRCFile.write(newStream.str(), newStream.pcount());
#endif
 
  // return to old pos
  in.seekg(oldPos);
  return true;
}
#endif //#if _ENABLE_PBO_PROTECTION

// computes crc from given range in stream
bool ComputeCRCRange(QIStream& in, CRCCalculator& calc, QFileSize startOffset, QFileSize size)
{
  static const int granularity = 1024*1024;

  if (in.fail())
    return false;

  // remember current pos
  QFileSize oldPos = in.tellg();
  
  in.seekg(startOffset);
  if (in.fail())
    return false;
  
  Temp<unsigned char> buffer(granularity);

  QFileSize done = 0;
  while (done < size)
  {
    int sizeToRead = min(buffer.Size(), (int)(size-done));
    int rd = in.read(buffer.Data(), sizeToRead);
    Assert(rd == sizeToRead);
    if (in.fail() || rd != sizeToRead)
    {
      in.seekg(oldPos);
      return false;
    }
    calc.Add(buffer.Data(), sizeToRead);
#if _WRITE_FILE_CRC
    outCRCFile.write(buffer.Data(), sizeToRead);
#endif

    done += sizeToRead;
  }
   
  // return to remembered position
  in.seekg(oldPos);

  return true;
}

/// compute src of given file
unsigned long ComputeCRC(const RString& fileName)
{
#if _ENABLE_PBO_PROTECTION
  // check if bank is protected and get prot info
  QFBank::BankHeaderProtectionInfo protInfo;
  bool isProtected = PBOHeaderProtection::IsProtectedPBO(fileName, true, &protInfo);
#endif //_ENABLE_PBO_PROTECTION

  // open bank for reading
  QIFStream in;
  in.open(fileName);

  if (in.fail())
    return false;

#if _WRITE_FILE_CRC
  RString outCrc = fileName + ".crc";
  outCRCFile.open(cc_cast(outCrc));
#endif

  // get stream size
  in.seekg(0, QIOS::end);
  QFileSize size = in.tellg();

  // seek back at start
  in.seekg(0);

  CRCCalculator crcCalc;
  crcCalc.Reset();

#if _ENABLE_PBO_PROTECTION
  if (isProtected)
  {
    ComputeCRCRange(in, crcCalc, 0, protInfo.protHeaderStart);
    ComputeCRCRangeProtected(fileName, in, crcCalc, protInfo.protHeaderStart, protInfo.protHeaderSize);
    QFileSize offset = protInfo.protHeaderStart + protInfo.protHeaderSize;
    ComputeCRCRange(in, crcCalc, offset, size - offset);
  }
  else
#endif
  {
    ComputeCRCRange(in, crcCalc, 0, size);
  }
  
#if _WRITE_FILE_CRC
  outCRCFile.close();
#endif
  return crcCalc.GetResult();
}

// parameters:
//   filename ... the name of the file to be compared to the entry in the crc.crc file
static CheckVersionResult CheckVersion
(
  EnumApplyContext &context, FindArrayRStringCI *currentDistribution, const char *filename=NULL, bool original=true,
  bool enableRetry=false
)
{
  ConsoleWindow *console = context.console;
  char inputPath[256];
  int verMajor = context.verMajor;
  int verMinor = context.verMinor;
  if (!original)
  {
    verMajor = context.verMajorPatch;
    verMinor = context.verMinorPatch;
  }
  sprintf(inputPath,"crc\\%d.%02d\\crc.crc",verMajor,verMinor);
  const char *f = OpenResourceText(inputPath,context.ctx);
  if (f)
  {
    // first, create the list of files to be checked (filter by filename and distributions)
    AutoArray<CheckVersionInfo> toCheck;
    while (true)
    {
      // read the single line of the file
      char line[1024];
      const char *eol = strchr(f,'\n');
      if (!eol) break;
      strncpy(line,f,eol-f);
      line[eol-f] = 0;
      f = eol+1;
      if (*line && line[strlen(line)-1]=='\r') line[strlen(line)-1]=0;

      CheckVersionInfo info;

      char *sp = strchr(line,' ');
      if (!sp) continue;
      *sp++=0;
      char *end;
      info.crc.Add(strtoul(sp,&end,16));
      while (isspace(*end))
      {
        while (isspace(*end)) end++;
        if (isxdigit(*end))
        {
          info.crc.Add(strtoul(end,&end,16));
        }
      }

      const char *sname = line;
      info.addon = (*sname=='+');
      if (info.addon) sname++;

      if (filename && strcmpi(sname,filename) != 0)
      {
        // check only given file
        // if filename is given, it contain the distribution extension
        continue;
      }

      // check for the distribution id
      info.distribution = INT_MAX;
      info.name = sname;
      const char *ext = strrchr(sname, '.');
      if (ext && *(ext + 1) == '#')
      {
        // distribution given
        info.name = RString(sname, ext - sname);
        if (currentDistribution)
        {
          info.distribution = currentDistribution->Find(ext + 2);
          if (info.distribution < 0) continue; // incompatible distribution
        }
      }

      info.path = RString(context.dir) + info.name;
      
      // Select only the most specific distribution
      int index = -1;
      for (int i=0; i<toCheck.Size(); i++)
      {
        if (stricmp(toCheck[i].name, info.name) == 0)
        {
          index = i; break;
        }
      }
      if (index >= 0)
      {
        // select one
        if (info.distribution < toCheck[index].distribution) toCheck[index] = info;
      }
      else toCheck.Add(info);
    }

    // check the files from the list
    CheckVersionResult noError = CVROK;
    bool printDone = true;
    CRCCalculator cc;
    for (int i=0; i<toCheck.Size(); i++)
    {
      const CheckVersionInfo &info = toCheck[i];

      FileMemMapped file;
      file.Open(info.path);
      console->SetProgress(IDS_CHECK_FILE, cc_cast(info.name));
      //console->ProcessMessages();
      if (file.GetError() )
      {
        if (!info.addon)
        {
          console->PrintF(IDS_MISSING_FILE, cc_cast(info.name));
          noError = CVRError;
          break;
        }
      }
      else
      {
        if (info.crc.Size()>0)
        {
          file.Close();
          unsigned int crc = ComputeCRC(info.path);
          bool match = false;
          for (int i=0; i<info.crc.Size(); i++)
          {
            if (crc==info.crc[i]) {match = true;break;}
          }
          if (!match)
          {
            if (!info.addon)
            {
              console->PrintF(IDS_ERROR_IN_FILE,cc_cast(info.name),info.crc[0],crc);
              RetryResult rr = AskRetry(
                console->GetHInstance(),console->GetHWnd(),
                enableRetry,IDS_ERROR_IN_FILE,cc_cast(info.name),info.crc[0],crc
              );
              if (rr==RRAbort)
              {
                noError = CVRError;
                break;
              }
              else if (rr==RRRetry)
              {
                noError = CVRRetry;
                break;
              }
            }
            else
            {
              console->PrintF(IDS_WARN_ERROR_IN_FILE,cc_cast(info.name),info.crc[0],crc);
            }
          }
        }
      }
      //console->ProcessMessages();
      if (console->Closed())
      {
        printDone = false;
        //console->PrintF(IDS_ABORT"User aborted verification.\r\n");
        break;
      }
    }
    console->SetProgress("");
    return noError;
  }
  else
  {
    if (!filename)
    {
      console->PrintF(IDS_NO_CRC_VER,verMajor,verMinor);
    }
    else
    {
      console->PrintF(IDS_NO_CRC_VER_FILE,filename,verMajor,verMinor);
    }
    return CVROK;
  }
}

static void ScanVersion(int &verMajor, int &verMinor, const char *version)
{
  verMajor = 0, verMinor = 0;
  char *end;
  verMajor = strtoul(version,&end,10);
  if (*end!='.')
  {
    verMinor = 0;
  }
  else
  {
    verMinor = strtoul(end+1,&end,10);
  }
}

static int ScanVersion(const char *text)
{
  int verMajor, verMinor;
  ScanVersion(verMajor,verMinor,text);
  return verMajor*100+verMinor;
}


static void GetFilenameString(const char *filename, char *str, RES_CONTEXT ctx)
{
  int size;
  void *ptr = FindAndLoadResource(size,ctx,filename);
  if (!ptr)
  {
    //MessageBox( NULL, filename, "Error in GetPatchVersion", MB_OK | MB_ICONINFORMATION);
    str[0] = 0;
    return;
  }
  memcpy(str,ptr,size);
  str[size]=0;
  
  char *p = strchr(str, '\r');
  while (p != NULL)
  {
    *p++ = '\0';
    p = strchr(p, '\r');
  }
  
  p = strchr(str, '\n');
  while (p != NULL)
  {
    *p++ = '\0';
    p = strchr(p, '\n');
  }
}


static void GetPatchVersion(const char *filename, char *version, RES_CONTEXT ctx)
{
  // find binary resource "version"
  *version=0;
  // read it as string

  int size;
  void *ptr = FindAndLoadResource(size,ctx,filename);
  if (!ptr)
  {
    //MessageBox( NULL, filename, "Error in GetPatchVersion", MB_OK | MB_ICONINFORMATION);
    return;
  }
  memcpy(version,ptr,size);
  version[size]=0;
  if (strchr(version,'\r')) *strchr(version,'\r')=0;
  if (strchr(version,'\n')) *strchr(version,'\n')=0;
}

static int GetPatchVersion(const char *filename,RES_CONTEXT ctx)
{
  char buf[1024];
  GetPatchVersion(filename,buf,ctx);
  if (!*buf) return 0;
  return ScanVersion(buf);
}

// Macrovision CD Protection
void __cdecl GetAppVersion(const char *app, char *version);
CDAPFN_DECLARE_GLOBAL(GetAppVersion, CDAPFN_OVERHEAD_L4, CDAPFN_CONSTRAINT_NONE);

void __cdecl GetAppVersion(const char *app, char *version)
{
  DWORD handle;
  DWORD bufferSize = GetFileVersionInfoSize(app, &handle);
  void *data = NULL;
  unsigned int dataSize = 0;
  char *buffer = new char[bufferSize];
  BOOL ok = GetFileVersionInfo(app, handle, bufferSize, buffer);
  if (ok)
  {
    VerQueryValue(buffer, TEXT("\\StringFileInfo\\040904b0\\FileVersion"), &data, &dataSize);
    strncpy(version, (char *)data, dataSize);
  }
  version[dataSize] = 0;
  delete [] buffer;

  CDAPFN_ENDMARK(GetAppVersion);
}

void GetAppVersion(const AutoArray<RString> &app, char *version)
{
  for (int i=0; i<app.Size(); i++)
  {
    GetAppVersion(cc_cast(app[i]), version);
    if (*version) break;
  }
}

#if _DEBUG

static BOOL CALLBACK EnumResNames(HMODULE hModule, LPCTSTR lpType, LPTSTR lpName, LONG lParam)
{ 
   // Write the resource name to a resource information file. 
   // The name may be a string or an unsigned decimal 
   // integer, so test before printing. 

  if ((ULONG)lpName & 0xFFFF0000) 
  { 
      LogF("\tName: %s", lpName);
  } 
  else 
  { 
      LogF("\tName: %u", (USHORT)lpName); 
  } 

  return TRUE; 
} 

/*
static BOOL CALLBACK EnumResTypes( HMODULE hModule, LPTSTR lpType, LONG lParam )
{
  if ((ULONG)lpType & 0xFFFF0000) 
  { 
    LogF("Type: %s", lpType); 
  } 
  else 
  { 
    LogF("Type: %u", (USHORT)lpType); 
  }


  EnumResourceNames(hModule, lpType, EnumResNames, lParam); 
 
  return TRUE; 
}
*/

#endif

#if PARSE_RESOURCE_OURSELVES

typedef BOOL CALLBACK EnumResNamesF(HMODULE hModule, LPCTSTR lpType, LPTSTR lpName, LONG lParam);

struct EnumApplyContext;

static bool EnumResourceNamesCustom(
  const ResContext &custCtx, char *type, EnumResNamesF *callback, const EnumApplyContext *context
)
{
  const ResContext *ctx = context->ctx;

  PIMAGE_RESOURCE_DIRECTORY_ENTRY resDirEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(ctx->resDir + 1);
  int i;
  for (i=0; i<ctx->resDir->NumberOfNamedEntries; i++, resDirEntry++)
  {
    char buffer[128];
    if (resDirEntry->NameIsString)
    {
      GetResourceNameFromId(resDirEntry->Name, ctx->base, buffer, sizeof(buffer));
      BOOL cont = callback(mod,type,buffer,(LONG)context);
      if (!cont) return false;

    }
  }
  return true;
}

#define EnumResourceNamesC EnumResourceNamesCustom

#else

#define EnumResourceNamesC(i,t,c,ctx) EnumResourceNames(i.hMod,t,c,(LONG)ctx)

#endif

BOOL CALLBACK EnumResNamesCollect(HMODULE hModule, LPCTSTR lpType, LPTSTR lpName, LONG lParam)
{ 
  EnumApplyContext *context = (EnumApplyContext *)lParam;
  // Write the resource name to a resource information file. 
  // The name may be a string or an unsigned decimal 
  // integer, so test before printing. 

  // if it has no name, ignore it
  if (((ULONG)lpName & 0xFFFF0000)==0) return TRUE;

  context->found = true;

  int verLen = strlen(context->version);
  // ignore files from other directories
  if (strnicmp(lpName,context->version,verLen)) return TRUE;

  context->files.Add(lpName);
  return TRUE;
}

// shortcuts
#include <shlobj.h>
#include <tchar.h>
#include <errno.h>
bool MakePath(LPCTSTR pPath, bool bForFile)
{
  TCHAR abspath[_MAX_PATH];
  _tcscpy(abspath,pPath);
  _tfullpath(abspath,pPath,_MAX_PATH);
  // cut off the file name if present
  if (bForFile)
  {
    int nLen = lstrlen(abspath);
    for(int i=(nLen-1);i>0; i--)
    {
      if ((abspath[i]==_T('\\'))||(abspath[i]==_T('/')))
      {
        abspath[i] = 0;
        break;
      }
    };
  };
  TCHAR  temp[_MAX_PATH];
  memset(temp,0,sizeof(temp));
  // vytvoøení cesty
  bool bDisk = FALSE;
  unsigned int nPos = 0;
  while(nPos<_tcslen(abspath))
  {
    if ((abspath[nPos]==_T('\\'))||(abspath[nPos]==_T('/')))
    {
      if (!bDisk)
      { // 1. '\\'
        temp[nPos] = abspath[nPos];
        nPos++;
        // sí -> 2x
        if ( (nPos<_tcslen(abspath)) &&
          ((abspath[nPos]==_T('\\'))||
          (abspath[nPos]==_T('/'))))
        {
          temp[nPos] = abspath[nPos];
          nPos++;
        }
        bDisk = TRUE;
      } else
      { // create path and continue
        if (_tchdir(temp)!=0)
        {
          if((_tmkdir(temp)==-1)&&(errno==ENOENT)) return FALSE;
          // add it to uninstall.log for delete
          GUninstalLog.Add(RString("D:")+temp);
        } 
        else
        {	// add it to uninstall.log for delete
          GUninstalLog.Add(RString("D:")+temp);
        };
        temp[nPos] = abspath[nPos];
        nPos++;
      }
    } else
    { // copy char
      temp[nPos] = abspath[nPos];
      nPos++;
      if (nPos>=_tcslen(abspath))
      { // last directory with no '\\'
        if (_tchdir(temp)!=0)
        {
          if((_tmkdir(temp)==-1)&&(errno==ENOENT)) return FALSE;
          // add it to uninstall.log for delete
          GUninstalLog.Add(RString("D:")+temp);
        } else
        {	// it exists, but we add it to uninstall.log for delete
          GUninstalLog.Add(RString("D:")+temp);
        };
      }
    }
  };
  return true;
}
  
static void ApplyPatch(EnumApplyContext &context)
{
  // we need temporary context when beta patch is used (CRC checking need to know proper destination dir in its context)
  EnumApplyContext applyContext = context;
  if (!applyContext.targetDir.IsEmpty())
  {
    strcat(applyContext.dir,applyContext.targetDir);
    if (applyContext.targetDir[applyContext.targetDir.GetLength()-1]!='\\')
      strcat(applyContext.dir, "\\");
  }
  for (int i=0; i<applyContext.files.Size(); i++)
  {
    const char *lpName = applyContext.files[i];
    Log("Apply : %s", lpName); 

    int verLen = strlen(applyContext.version);

    char destination[1024];
    strcpy(destination,applyContext.dir);
    const char *dname = destination + strlen(destination);

    strcat(destination,lpName+verLen);

    // remove the distribution extension if present
    char *ext = strrchr(destination, '.');
    if (ext && *(ext + 1) == '#') *ext = 0;

    if (stricmp(ext, ".del-silent")==0)
    {
      *ext = '\0'; // remove extension from "destination" 
      remove(destination);
      continue;
    }


    applyContext.console->SetProgress(dname);
    
    RString destinationLow = destination; 
    destinationLow.Lower();
    if (_access(destinationLow, 0)==-1)
    {
      //in the GUninstalLog the files must be with the .tmp extension
      RString destinationTmp = destinationLow + RString(".tmp");
      MakePath(destinationTmp, true);
      GUninstalLog.Add(RString("F:")+destinationTmp); //new file need to be added into uninstall log
    }
Retry:	
    if (!ResourceToFile(lpName,destinationLow,applyContext.ctx))
    {
      RetryResult rr = AskRetry(
        applyContext.console->GetHInstance(),applyContext.console->GetHWnd(),
        true,IDS_ERROR_IN_COPY,lpName);
      if (rr == RRAbort)
      {
        context.error = true;
        strcpy(context.errorFile, dname);
        return;
      }
      else if (rr == RRRetry)
      {
        goto Retry;
      }
      else continue;
    }
#if _ENABLE_PBO_PROTECTION
    else
    {
      bool isProtectedPBO = PBOHeaderProtection::IsProtectedPBO(destinationLow);
      if (isProtectedPBO)
      {
        if (!PBODecryptSetupEncryptSerial(destinationLow))      
        {
          context.error = true;
          strcpy(context.errorFile,lpName);
          return;
        }
      }
    }
#endif

    // check the crc of the result (filename with the product extension is given, so we need not to pass distributions)
    CheckVersionResult ret = CheckVersion(applyContext, NULL, lpName+verLen, false, true);

    if (ret==CVRRetry) goto Retry;
    if (ret!=CVROK)
    {
      context.error = true;
      strcpy(context.errorFile,dname);
      return;
    }
  }
} 

static bool CheckDisk(const char *source,size_t need)
{
  // source should be full path

  char disc[10];
  strncpy(disc,source,3);
  disc[3] = 0;

  DWORD cluster, sector, free, total;
  GetDiskFreeSpace(disc, &cluster, &sector, &free, &total);
  __int64 avail = (__int64)cluster * (__int64)sector * (__int64)free;
  return avail>=need;
}

static bool IncrementalCheckSpace(EnumApplyContext &context, __int64 &patchBytesCount)
{ 
  for (int i=0; i<context.files.Size(); i++)
  {
    context.found = true;
    const char *lpName = context.files[i];

    // ignore target version information
    int verLen = strlen(context.version);
    const char *tgtVer = lpName + verLen;

    char *end;
    strtoul(tgtVer+1,&end,10);
    if (*end!='.') continue;
    strtoul(end+1,&end,10);
    if (*end!='\\') continue;

    verLen = end +1 - lpName;

    // construct full source, destination and update filenames

    char destination[1024];
    char source[1024];
    char update[1024];
    strcpy(destination,context.dir);
    strcat(destination,lpName+verLen);
    char *ext = strrchr(destination, '.');
    if (ext && *(ext + 1) == '#')
    {
      // remove the distribution extension
      *ext = 0;
    }

    strcpy(source,context.dir);
    strcat(source,lpName+verLen);
    ext = strrchr(source, '.');
    if (ext && *(ext + 1) == '#')
    {
      // remove the distribution extension
      *ext = 0;
    }

    strcpy(update,context.dir);
    strcat(update,lpName+verLen);

    strcpy(GetFileExt(destination),".tmp");
    strcpy(GetFileExt(source),"");

    // check source file name

    size_t patchsize = ResourceSize(lpName,context.ctx);
    int h = open(source,O_RDONLY|O_BINARY);
    if (h < 0) 
    { // source file does not exist, update patchBytesCount (.new=patchsize, .del,.equ,.ini=patchsize ... which is 1)
      patchBytesCount += patchsize;
      continue; // will be handled by the patch application
    }
    size_t sourcesize = filelength(h);
    close(h);
    patchBytesCount += ((patchsize==1) ? patchsize : (sourcesize + patchsize));
    // assume we need size_of_original_file*2 + size_of_patch + some reserve
    size_t need = sourcesize*2+patchsize+10*1024*1024;
    if (!CheckDisk(source,need)) return false;
  }
  return true;
} 

static void UpdatePatchProgressBar(EnumApplyContext &context, const char *src, const char *tgt, const char *upd)
{
  int currentSize = 0;
  int h = open(upd,O_RDONLY|O_BINARY);
  if (h>=0) 
  { 
    currentSize += filelength(h);
    close(h);
  }
  if (currentSize!=1)
  {
    h = open(src,O_RDONLY|O_BINARY);
    if (h>=0)
    {
      currentSize += filelength(h);
      close(h);
    }
  }
  context.console->SetProgressBar(currentSize); //it will updates its currentSize and progress bar control
}

LPCTSTR MakeLongDirStr(LPTSTR dest,LPCTSTR source)
{
  if (dest!=source) _tcscpy(dest,source);
  int len;
  if ((len = _tcslen(dest))!=0)
  {
    if ((dest[len-1]!=_T('\\'))&&(dest[len-1]!=_T('/')))
      _tcscat(dest,_T("\\"));
  }
  return dest;
}

static bool CreateLink(
  const char *pLinkFile, const char *pFileObj, const char *pDescription, 
  const char *pPath, const char *pArguments
)
{
  HRESULT hres; 
  IShellLink* psl; 

  // Get a pointer to the IShellLink interface. 
  hres = CoCreateInstance(CLSID_ShellLink, NULL, 
    CLSCTX_INPROC_SERVER, IID_IShellLink,(void**) &psl); 
  if (SUCCEEDED(hres)) { 
    IPersistFile* ppf; 

    // Set the path to the shortcut target, and add the 
    // description. 
    //psl->lpVtbl->SetPath(psl, pFileObj); 
    psl->SetPath(pFileObj); 
    //psl->lpVtbl->SetDescription(psl, pDescription); 
    psl->SetDescription(pDescription); 
    psl->SetArguments(pArguments);

    // current directory
    {
      char buff[1024]; _tcscpy(buff,pPath);
      int nLen = _tcslen(buff);
      for(int i=(nLen-1);i>0; i--)
      {
        if ((buff[i]=='\\')||(buff[i]=='/'))
        {
          buff[i] = 0;
          break;
        }
      };
      psl->SetWorkingDirectory(buff); 
    }

    // Query IShellLink for the IPersistFile interface for saving the 
    // shortcut in persistent storage. 

    //hres = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, 
    //    &ppf); 
    hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf); 

    if (SUCCEEDED(hres)) { 
      wchar_t wsz[MAX_PATH]; 

      // Ensure that the string is UNICODE. 
      MultiByteToWideChar(CP_ACP, 0, pLinkFile, -1, wsz, MAX_PATH);

      // Save the link by calling IPersistFile::Save. 
      //hres = ppf->lpVtbl->Save(ppf, wsz, TRUE); 
      hres = ppf->Save(wsz, TRUE); 
      //ppf->lpVtbl->Release(ppf); 
      ppf->Release(); 
    } 
    //psl->lpVtbl->Release(psl); 
    psl->Release(); 
  } 
  return SUCCEEDED(hres); 
}

struct ShortcutInfo
{
  RString name;
  RString fileName;
  RString fileObj;
  RString arguments;
  RString workingDirectory;
  RString startMenuGroupDirectory;
  RString description;
  AutoArray<RString> includeDistributions;
  AutoArray<RString> excludeDistributions;
  void Load(ParamEntryVal cls);
};

void ShortcutInfo::Load(ParamEntryVal cls)
{
  if (!cls.IsClass()) return;

  name = cls.GetName();
  fileName = cls >> "source";
  fileObj = cls >> "destination";
  arguments = cls >> "arguments";
  workingDirectory = cls >> "workingDirectory";
  startMenuGroupDirectory = cls >> "startMenuGroup";
  description = cls >> "description";
  ParamEntryVal includeDist = cls.FindEntry("includeDistributions");
  if (includeDist.GetPointer() && includeDist.IsArray())
  {
    includeDistributions.Realloc(includeDist.GetSize());
    for (int i=0; i<includeDist.GetSize(); i++)
    {
      includeDistributions.AddFast(includeDist[i].GetValue());
    }
  }
  ParamEntryVal excludeDist = cls.FindEntry("excludeDistributions");
  if (excludeDist.GetPointer() && excludeDist.IsArray())
  {
    excludeDistributions.Realloc(excludeDist.GetSize());
    for (int i=0; i<excludeDist.GetSize(); i++)
    {
      excludeDistributions.AddFast(excludeDist[i].GetValue());
    }
  }
  if (description.IsEmpty()) description = name;
}

static void CreateShortcut(
  HWND hWnd, ShortcutInfo sInfo,
  AutoArray<RString> &uninstallLog
)
{
  CoInitialize(NULL);
  // directory for shortcuts
  char shellFile[1024];
  *shellFile = 0; //init
  LPITEMIDLIST	ppidl;

  // Start menu folder location
  int folder = CSIDL_COMMON_PROGRAMS; //m_bAllUsers ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS;
  bool startMenuShorcut = (*sInfo.startMenuGroupDirectory != 0);
  HRESULT hres = startMenuShorcut ? SHGetSpecialFolderLocation(hWnd, folder, &ppidl) : S_OK;
  if (SUCCEEDED(hres))
  {
    if (startMenuShorcut)
    {
      SHGetPathFromIDList( ppidl, shellFile );

      MakeLongDirStr(shellFile,shellFile);
      strcat(shellFile,sInfo.startMenuGroupDirectory);
      MakeLongDirStr(shellFile,shellFile);
    }
    // create shortcuts
    strcat(shellFile, sInfo.fileName);
    strcat(shellFile, ".lnk");

    MakePath(shellFile,true);
    CreateLink(shellFile, sInfo.fileObj, sInfo.description, sInfo.workingDirectory, sInfo.arguments);
    uninstallLog.Add(RString("F:")+shellFile);
  }

  CoUninitialize();
}

static bool IncrementalPatchApply(EnumApplyContext &context,const bool byCrc = false)
{ 
  for (int i=0; i<context.files.Size(); i++)
  {
    context.found = true;
    const char *lpName = context.files[i];

    Log("Incremental apply : %s", lpName); 


    // ignore target version information
    int verLen = strlen(context.version);
    const char *tgtVer = lpName + verLen;

    unsigned int verMaj,verMin;
    if (byCrc)
    {
      verLen++;
    }
    else
    {
    char *end;
      verMaj = strtoul(tgtVer,&end,10);
    if (*end!='.') continue;
      verMin = strtoul(end+1,&end,10);
    if (*end!='\\') continue;
    verLen = end +1 - lpName;
    }

    // construct full source, destination and update filenames

    char destination[1024];
    char source[1024];
    char update[1024];
    char destinationTgt[1024];
    RString tgtFile; //used only when source file is updated into another folder
    bool deleteSourceFile = true; //only when source file is updated into another folder, it is not deleted
#if USE_DELTA3_BINARY_DIFF
    RString rscFileName = lpName+verLen;
    //ext must be .upd or .new or .del
    char *ext = GetFileExt(rscFileName.MutableData());
    if (ext && (ext[1]=='#' || ext[1]=='!')) *ext=0; //clear the distribution extension
    strcpy(destination, context.dir);
    strcat(destination, rscFileName);
    // when incremental BETA patching is used and file already exists in tgt directory, it should be excluded 
    // for the sake of CRC checking.
    int tgtOffset = 0; // tgtOffset is used to remove the target directory
    if (!context.targetDir.IsEmpty())
    { //it should be installed into directory specified
      strcpy(destinationTgt,context.dir);
      strcat(destinationTgt, context.targetDir);
      if (context.targetDir[context.targetDir.GetLength()-1]!='\\')
        strcat(destinationTgt, "\\");
      //test whether file in destinationTgt subdirectory already exists
      tgtFile = destinationTgt;
      tgtFile = tgtFile + rscFileName;
      tgtFile.Lower();
      strcpy(GetFileExt(tgtFile.MutableData()),"");
      MakePath(tgtFile, true); //we need to add new dir names to uninstall.log
      if (_access(tgtFile, 0)==0)
      { //file exists already - take it as source / destination
        strcpy(destination, destinationTgt);
        strcat(destination, rscFileName);
        strcpy(source, destination);
        tgtOffset = strlen(context.targetDir)+1; //+1 for the '\\' char
      }
      else
      { //file does not exist yet - source should be taken as ordinary, but destination should be changed
        strcpy(source, destination);
        strcpy(destination, destinationTgt);
        strcat(destination, rscFileName);
        _strlwr(destination);
        //moreover, source file should not be deleted!
        deleteSourceFile = false;
      }
      strcpy(update,destinationTgt);
      strcat(update,lpName+verLen);
      _strlwr(update);
    }
    else 
    {
      strcpy(source, destination);
      strcpy(update,context.dir);
      strcat(update,lpName+verLen);
    }
    const char *sname = source+strlen(context.dir)+tgtOffset;
#else
    strcpy(destination,context.dir);
    strcat(destination,lpName+verLen);
    char *ext = strrchr(destination, '.');
    if (ext && *(ext + 1) == '#')
    {
      // remove the distribution extension
      *ext = 0;
    }

    strcpy(source,context.dir);
    const char *sname = source + strlen(source);
    strcat(source,lpName+verLen);
    ext = strrchr(source, '.');
    if (ext && *(ext + 1) == '#')
    {
      // remove the distribution extension
      *ext = 0;
    }
    strcpy(update,context.dir);
    strcat(update,lpName+verLen);
#endif

    if (!byCrc)
    {
    strcpy(GetFileExt(destination),".tmp");
    strcpy(GetFileExt(source),"");
    }
    else
    {
      strcat(destination,".tmp");
    }
    context.console->SetProgress(sname);

    if (!ResourceToFile(lpName,update,context.ctx))
    {
      context.error = true;
      strcpy(context.errorFile,source);
      return false;
    }

    // preserve source filename case
    _finddata_t data;
    long h = _findfirst(source,&data);
    if (h!=-1)
    {
      strcpy(GetFilenameExt(source),data.name);
      _findclose(h);
    }
    // FIX: even when source file is missing, we can apply the patch sometimes (if the patch adds the file)
/*
    else
    {
      // fatal error: source file is missing
      context.error = true;
      strcpy(context.errorFile,source);
      return false;
    }
*/

    // update has to be done to some temporary file
    Retry:
    IPatchAction *action = CreatePatchActionApply(GUninstalLog, !context.targetDir.IsEmpty());
    IPatchAction::Result ok = action->PerformByName(destination,update,source);
    //update progress bar
    UpdatePatchProgressBar(context, source, destination, update);
#if 1
    if (ok == IPatchAction::Error)
    {
      // report the exact error cause
      context.console->PrintF(IDS_ERROR_LOG, lpName, cc_cast(action->GetErrorMessage()));
    }
#endif
    delete action;

    if (ok==IPatchAction::Error)
    {
      // resulting file is bad
      RetryResult rr = AskRetry(
        context.console->GetHInstance(),context.console->GetHWnd(),
        true,IDS_ERROR_IN_FILE_NOCRC,sname
      );
      if (rr==RRRetry)
      {
        goto Retry;
      }
      else if (rr==RRIgnore)
      {
        ok = IPatchAction::OKInPlace;
      }
      else
      {
        unlink(destination);
      }
    }
    // if needed, rename temp file
    if (ok==IPatchAction::OK)
    {
      bool sourceIsNew = (chmod(source,S_IREAD | S_IWRITE)!=0);
      if (deleteSourceFile) unlink(source);
      if (context.targetDir.IsEmpty())
      {
        RString sourceLow = source;
        if (sourceIsNew) sourceLow.Lower();
        if (rename(destination,sourceLow)!=0)
        {
          context.error = true;
          strcpy(context.errorFile,sname);
        }
        else
        { //perform CRC check
          EnumApplyContext betaContext = context;
          betaContext.verMajor = verMaj; //we need to check the correct, middle step, version
          betaContext.verMinor = verMin;
          CheckVersionResult ret = CVROK;
          // we don't have crc for diffs from DFF folder, but patch will check them all at the end
          if (!byCrc)
            CheckVersion(betaContext, NULL, sname, true, true);
          if (ret!=CVROK)
          {
            context.error = true;
            strcpy(context.errorFile,sname);
          }
        }
      }
      else
      {
        if (rename(destination,tgtFile)!=0)
        {
          context.error = true;
          strcpy(context.errorFile,sname);
        }
        else
        { //perform CRC check
          EnumApplyContext betaContext = context;
          strcat(betaContext.dir, context.targetDir);
          if (betaContext.targetDir[betaContext.targetDir.GetLength()-1]!='\\')
            strcat(betaContext.dir, "\\");          
          betaContext.verMajor = verMaj; //we need to check the correct, middle step, version
          betaContext.verMinor = verMin;
          CheckVersionResult ret = CheckVersion(betaContext, NULL, sname, true, true);
          if (ret!=CVROK)
          {
            context.error = true;
            strcpy(context.errorFile,sname);
          }
        }
      }
      //MoveFileEx(destination,source,MOVEFILE_REPLACE_EXISTING);
    }
    else if (ok==IPatchAction::OKInPlace)
    {
      // if patching from DFF it cannot continue because of loop with same CRC causes same error
      if (byCrc) context.error = true;
    }
    else if (ok==IPatchAction::OKRemoved)
    {
      unlink(source);
    }
    else
    {
      context.error = true;
      strcpy(context.errorFile,sname);
    }
    // we always want to remove the upd file
    unlink(update);

    if (context.error) return false;
  }
  return true;
}

static void ApplyError(ConsoleWindow *console, int phase, const char *file)
{
  console->PrintF(IDS_UPGRADE_FAILED,file, phase);
  console->SetWaitForClosing();
}

/*
void ProcessPatchMessages()
{
  MSG msg;
  while( PeekMessage(&msg, 0, 0, 0, PM_REMOVE) )
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
*/

#if GOLD_UPGRADE
static void CreateUninstall(char *dir)
{
  char app[256];
  strcpy(app, dir);
  strcat(app, "uninstallGold.exe");
  const char *regKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Operation Flashpoint Gold Upgrade";
  const char *display = "Operation Flashpoint Gold Upgrade uninstall";

  HKEY key;
  if
  (
    ::RegCreateKey(HKEY_LOCAL_MACHINE, regKey, &key) == ERROR_SUCCESS
  )
  {
    ::RegSetValueEx(key, "UninstallString", 0, REG_SZ, (BYTE *)app, strlen(app) + 1);
    ::RegSetValueEx(key, "DisplayName", 0, REG_SZ, (BYTE*)display, strlen(display) + 1);
    ::RegCloseKey(key);
  }
}
#endif

struct DistributionInfo
{
  RString id;
  RString name;
  int scope;
  int parentIndex;
};

TypeIsMovableZeroed(DistributionInfo)

void LoadDistributions(ParamEntryPar versionInfo, AutoArray<DistributionInfo> &distributions, FindArrayRStringCI &currentDistribution)
{
  ConstParamEntryPtr entry = versionInfo.FindEntry("Distributions");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal cls = entry->GetEntry(i);
      if (!cls.IsClass()) continue;

      DistributionInfo &info = distributions.Append();
      info.id = cls.GetName();
      info.scope = cls >> "scope";
      if (info.scope > 0)
      {
        info.name = cls >> "name";
        // all public distributions are possible
        currentDistribution.Add(info.id);
      }
      info.parentIndex = -1;
      RString parent = cls >> "parent";
      if (parent.GetLength() > 0)
      {
        for (int j=0; j<distributions.Size()-1; j++)
        {
          if (stricmp(parent, distributions[j].id) == 0)
          {
            info.parentIndex = j;
            break;
          }
        }
      }
    }
  }
}

#define BACKUP_DISTRIBUTIONS 0
void FindDistribution(ParamEntryPar versionInfo, RString dir, FindArrayRStringCI &currentDistribution)
{
  ConstParamEntryPtr entry = versionInfo.FindEntry("WhatDistribution");
  if (entry)
  {
#if BACKUP_DISTRIBUTIONS
    FindArrayRStringCI currentDistributionBackup = currentDistribution;
#endif
    int ruleAppliedCnt = 0;
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal rule = entry->GetEntry(i);
      if (!rule.IsClass()) continue;

      // check the conditions
      RString file = rule >> "file";
      QIFStream in;
      in.open(dir + file);
      if (in.fail()) continue;

      CRCCalculator calculator;
      calculator.Reset();
      in.copy(calculator);

      int crcStored = rule >> "crc";
      if (calculator.GetResult() != crcStored) continue;

      // apply the rule
      ParamEntryVal array = rule >> "distributions";
      FindArrayRStringCI validDistributions;
      int m = array.GetSize();
      validDistributions.Realloc(m);
      validDistributions.Resize(m);
      for (int j=0; j<m; j++) validDistributions[j] = array[j];

      // remove all distributions not in the list of valid distributions
      for (int k=0; k<currentDistribution.Size();)
      {
        RString check = currentDistribution[k];
        if (validDistributions.FindKey(check) >= 0) k++;
        else currentDistribution.DeleteAt(k);
      }
      ruleAppliedCnt++;
      if (currentDistribution.Size() == 0) 
      {
#if BACKUP_DISTRIBUTIONS
        if (ruleAppliedCnt>1) //FIX: multiple matches mishmash make all distributions possible
        {
          currentDistribution = currentDistributionBackup;
        }
#endif
        break;
      }
    }
  }
}

class SelectDistributionDialog
{
protected:
  AutoArray<DistributionInfo> _distributions;
  AutoArray<RString> _currentDistribution;

  RString _result;

  HWND _list;

public:
  SelectDistributionDialog(HINSTANCE instance, AutoArray<DistributionInfo> &distributions, FindArrayRStringCI &currentDistribution);
  RString GetResult() const {return _result;}

  int DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
  static int CALLBACK DlgProcCB(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

SelectDistributionDialog::SelectDistributionDialog(HINSTANCE instance, AutoArray<DistributionInfo> &distributions, FindArrayRStringCI &currentDistribution)
{
  _distributions = distributions;
  _currentDistribution = currentDistribution;
  _list = NULL;

  DialogBoxParam(instance, MAKEINTRESOURCE(IDD_DISTRIBUTION), NULL, SelectDistributionDialog::DlgProcCB, (LPARAM)this);
}

int SelectDistributionDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_INITDIALOG:
    {
      _list = GetDlgItem(hDlg, IDC_DISTRIBUTIONS);
      // fill the listbox
      for (int i=0; i<_currentDistribution.Size(); i++)
      {
        RString id = _currentDistribution[i];
        // find the description
        RString name = id;
        for (int j=0; j<_distributions.Size(); j++)
        {
          if (stricmp(id, _distributions[j].id) == 0)
          {
            name = _distributions[j].name;
            break;
          }
        }
        int index = ::SendMessage(_list, LB_ADDSTRING, 0, (LPARAM)cc_cast(name));
        if (index < 0) continue;
        ::SendMessage(_list, LB_SETITEMDATA, index, i);
      }
      ::SendMessage(_list, LB_SETCURSEL, 0, 0);
      return TRUE;
    }
  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK) 
    {
      int index = ::SendMessage(_list, LB_GETCURSEL, 0, 0);
      if (index >= 0)
      {
        int i = ::SendMessage(_list, LB_GETITEMDATA, index, 0);
        if (i >= 0) _result = _currentDistribution[i];
      }
      EndDialog(hDlg, IDOK);
      return TRUE;
    }
    else if (LOWORD(wParam) == IDCANCEL)
    {
      EndDialog(hDlg, IDCANCEL);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

int CALLBACK SelectDistributionDialog::DlgProcCB(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_INITDIALOG)
  {
    SetWindowLong(hDlg, GWL_USERDATA, lParam);
    SelectDistributionDialog *dlg = (SelectDistributionDialog *)lParam;
    return dlg->DlgProc(hDlg, message, wParam, lParam);
  }
  else
  {
    SelectDistributionDialog *dlg = (SelectDistributionDialog *)GetWindowLong(hDlg, GWL_USERDATA);
    if (!dlg) return FALSE;
    return dlg->DlgProc(hDlg, message, wParam, lParam);
  }
}

static RString RemoveDistrAndUpdExtensions(RString file)
{
  const char *ext = strrchr(file, '.');
  RString outfile = file;
  if (ext && *(ext + 1) == '#') outfile = file.Substring(0, ext - cc_cast(file));
  char *updExt = GetFileExt(outfile.MutableData());
  if (updExt) *updExt = 0; //clear also .upd, .ini, .equ, .del, .new
  return outfile;
}

void FilterByDistributions(AutoArray<RString> &files, FindArrayRStringCI &currentDistribution, bool isIncremental=false)
{
  for (int i=0; i<files.Size(); i++)
  {
    RString file = files[i];
    // initialize - no distribution
    RString fileRaw = file;
    int bestDist = currentDistribution.Size();
    // check the distribution extension
    const char *ext = strrchr(file, '.');
    if (ext && *(ext + 1) == '#')
    {
      fileRaw = file.Substring(0, ext - cc_cast(file));
      bestDist = currentDistribution.FindKey(ext + 2);
      if (bestDist < 0) bestDist = currentDistribution.Size() + 1; // easier comparation 
    }
    // try to find the same file with the more specific distribution
    AutoArray<int> indices;
    for (int j=i+1; j<files.Size(); j++)
    {
      RString check = files[j];
      // initialize - no distribution
      RString checkRaw = check;
      int dist;
      // check the distribution extension
      const char *ext = strrchr(check, '.');
      if (ext && *(ext + 1) == '#')
      {
        checkRaw = check.Substring(0, ext - cc_cast(check));
        if (isIncremental)
        {
          RString checkRawNoExt = RemoveDistrAndUpdExtensions(checkRaw);
          RString fileRawNoExt = RemoveDistrAndUpdExtensions(fileRaw);
          if (stricmp(checkRawNoExt, fileRawNoExt) != 0) continue;
        }
        else if (stricmp(checkRaw, fileRaw) != 0) continue;
        dist = currentDistribution.FindKey(ext + 2);
        if (dist < 0) dist = currentDistribution.Size() + 1; // easier comparation 
      }
      else
      {
        if (isIncremental)
        {
          RString checkRawNoExt = RemoveDistrAndUpdExtensions(checkRaw);
          RString fileRawNoExt = RemoveDistrAndUpdExtensions(fileRaw);
          if (stricmp(checkRawNoExt, fileRawNoExt) != 0) continue;
        }
        else if (stricmp(checkRaw, fileRaw) != 0) continue;
        dist = currentDistribution.Size();
      }
      indices.Add(j);
      if (dist < bestDist) 
      {
        bestDist = dist;
        if (isIncremental)
        {
          //we need also to preserve the correct .upd, .new, .del, .ini, .equ extension
          fileRaw = checkRaw;
        }
      }
    }
    // select only the best distribution
    for (int j=indices.Size()-1; j>=0; j--) files.Delete(indices[j]);
    if (bestDist < currentDistribution.Size())
    {
      files[i] = fileRaw + RString(".#") + currentDistribution[bestDist];
      files[i].Upper();
    }
    else if (bestDist == currentDistribution.Size())
    {
      files[i] = fileRaw;
    }
    else
    {
      files.Delete(i);
      i--;
    }
  }
}

void FilterByCRC(AutoArray<RString> &files)
{
  FindArray<RString> filesCpy;
  for (int i=0; i<files.Size(); i++)
  {
    RString name = files[i];
    char *ext = strrchr(name.MutableData(), '.');
    if (ext && *(ext + 1) == '!')
      *ext =0;
    
    filesCpy.AddUnique(name);
  };
  files.Resize(0);
  files.Move(filesCpy);
}

void UpdateRegistry(ParamEntryPar versionInfo, RString distribution)
{
  ConstParamEntryPtr entry = versionInfo.FindEntry("UpdateRegistry");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal cls = entry->GetEntry(i);
      if (!cls.IsClass()) continue;

      ConstParamEntryPtr array = cls.FindEntry("distributions");
      if (array)
      {
        bool found = false;
        for (int j=0; j<array->GetSize(); j++)
        {
          RString check = (*array)[j];
          if (stricmp(check, distribution) == 0)
          {
            found = true;
            break;
          }
        }
        if (!found) continue;
      }

      bool registryUser = cls >> "registryUser";
      RString registryPath = cls >> "registryPath";
      RString registryName = cls >> "registryName";
      RString registryValue = cls >> "registryValue";

      HKEY rootKey = registryUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
      HKEY key;
      if (RegCreateKeyEx(rootKey, registryPath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
      {
        continue;
      }
      if (RegSetValueEx(key, registryName, 0, REG_SZ, (LPBYTE)cc_cast(registryValue), registryValue.GetLength() + 1) != ERROR_SUCCESS)
      {
        RegCloseKey(key);
        continue;
      }
      RegCloseKey(key);
    }
  }
}

#include <sys/stat.h>
static void DeleteDirectoryStructure(const char *name, bool deleteDir = true)
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

/*
int WINAPI PosBrowseCallbackProc(HWND hwnd,UINT uMsg,LPARAM lParam,LPARAM lpData)
{
  if (uMsg == BFFM_INITIALIZED)
  {
    ::SendMessage(hwnd,BFFM_SETSELECTION,TRUE,(LPARAM)((LPCTSTR)((CWizardPageDir*)lpData)->m_sDirectory));
    ::SendMessage(hwnd,BFFM_SETSTATUSTEXT,0,(LPARAM)((LPCTSTR)((CWizardPageDir*)lpData)->m_sDirectory));
  } else
    if (uMsg == BFFM_SELCHANGED)
    {
      TCHAR buff[_MAX_PATH];
      if (SHGetPathFromIDList((LPITEMIDLIST)lParam,buff))
      {
        CString sPath = buff;
        ((CWizardPageDir*)lpData)->m_pSetup->AddInstallationSubdirectory(sPath);
        ::SendMessage(hwnd,BFFM_SETSTATUSTEXT,0,(LPARAM)((LPCTSTR)sPath));
      }
    }
    return 0;
};
*/

static bool AskInstallDirectory(HINSTANCE inst, HWND wnd, char *dir, int dirSize)
{
  char caption[256];
  LoadAppString(inst, IDS_CAPTION, caption, sizeof(caption));

  char buf[2048];
  LoadAppString(inst, IDS_NOT_INSTALLED, buf, sizeof(buf));

  int result = MessageBox(wnd, buf, caption, MB_ICONQUESTION | MB_YESNO);
  if (result == IDNO) return false;

  // GetCurrentDirectory(dirSize, dir);
  // return true;

  if (FAILED(CoInitialize(NULL))) return false;

  BROWSEINFO browseInfo; 
  memset(&browseInfo, 0, sizeof(browseInfo));

  browseInfo.hwndOwner = wnd;
  // browseInfo.ulFlags = BIF_STATUSTEXT;
  // browseInfo.lpfn = (BFFCALLBACK)(BrowseCallback);
  // browseInfo.lParam = 0;

  bool ok = false;
  LPITEMIDLIST itemList = SHBrowseForFolder(&browseInfo);
  if (itemList)
  {
    ok = SHGetPathFromIDList(itemList, dir) == TRUE;
    CoTaskMemFree(itemList);
  }
  CoUninitialize();

  return ok;
}

#include <Es/Files/filenames.hpp>
static void ChangeToFullPath(RString &path, const RString rootDir)
{
  //if path is full path already, return with no change
  if (!IsRelativePath(cc_cast(path))) return;
  char full[1024];
  strcpy(full, cc_cast(rootDir));
  TerminateBy(full, '\\');
  strcat(full, path);
  path = full;
}

static void ApplyShortcuts(HWND hWnd, ParamEntryPar versionInfo, RString rootDir, AutoArray<RString> &uninstallLog, RString distribution)
{
  ConstParamEntryPtr entry = versionInfo.FindEntry("Shortcuts");
  if (entry)
  {
    for (int i=0; i<entry->GetEntryCount(); i++)
    {
      ParamEntryVal cls = entry->GetEntry(i);
      ShortcutInfo sInfo; sInfo.Load(cls);
      // relative paths should be changed to full paths using rootDir
      ChangeToFullPath(sInfo.workingDirectory, rootDir);
      ChangeToFullPath(sInfo.fileObj, rootDir);
      // when startMenuGroupDirectory is not used, *.lnk file will be created in the rootDir+destination
      if (sInfo.startMenuGroupDirectory.IsEmpty())
        ChangeToFullPath(sInfo.fileName, rootDir);
      // test whether this shortcut should be installed by checking distribution include/exclude lists
      bool addShortcut = true;
      if (sInfo.includeDistributions.Size()>0)
      {
        addShortcut = false;
        for (int dist=0; dist<sInfo.includeDistributions.Size(); dist++)
        {
          if (stricmp(distribution, sInfo.includeDistributions[dist])==0)
          {
            addShortcut = true;
            break;
          }
        }
        if (!addShortcut) continue;
      }
      if (sInfo.excludeDistributions.Size()>0)
      {
        for (int dist=0; dist<sInfo.excludeDistributions.Size(); dist++)
        {
          if (stricmp(distribution, sInfo.excludeDistributions[dist])==0)
          {
            addShortcut = false;
            break;
          }
        }
      }
      if (addShortcut)
        CreateShortcut(hWnd, sInfo, uninstallLog);
    }
  }
}

void ApplyUninstall(AutoArray<RString> &uninstallLog, RString dir, AutoArray<RString> namesList)
{
  if (uninstallLog.Size()>0)
  {
    if (!dir.IsEmpty())
    {
      for (int j = 0; j < namesList.Size(); j++) 
      {
        RString uninstallFileName = dir;
        for (int i = 0; i < uninstallLog.Size(); i++)
        {
          if (stricmp(GetFileExt(cc_cast(uninstallLog[i])), ".tmp")==0)
            *GetFileExt(uninstallLog[i].MutableData()) = 0; //.tmp extension cleared
        }
        if (uninstallFileName[uninstallFileName.GetLength()-1]=='\\') 
          uninstallFileName.MutableData()[uninstallFileName.GetLength()-1]=0;
        uninstallFileName = uninstallFileName + "\\"+ namesList[j];
        // clear duplicated lines first
        FILE *unLogFile = fopen(uninstallFileName,"rt");
        if (unLogFile)
        {
          AutoArray<RString> lines;
          char line[1024];
          while (fgets(line, sizeof(line)/sizeof(char), unLogFile))
          {
            char *lastChar; 
            do {
              lastChar = *line==0 ? line : line + strlen(line)-1;
              if (*lastChar && (*lastChar=='\n' || *lastChar=='\r')) *lastChar=0;
              lastChar = *line==0 ? line : line + strlen(line)-1 ;
            } while (*lastChar && (*lastChar=='\n' || *lastChar=='\r'));
            lines.Add(line);
          }
          fclose(unLogFile);
          for (int i=uninstallLog.Size()-1; i>=0; i--)
          {
            for (int j=0; j<i; j++)
            {
              if (stricmp(cc_cast(uninstallLog[j]),cc_cast(uninstallLog[i]))==0)
              {
                uninstallLog[i] = ""; //which is equivalent to removal
                break;
              }
            }
            if (!uninstallLog[i].IsEmpty())
            {
              for (int j=0; j<lines.Size(); j++)
              {
                if (stricmp(cc_cast(lines[j]),cc_cast(uninstallLog[i]))==0)
                {
                  uninstallLog[i] = ""; //which is equivalent to removal
                  break;
                }
              }
            }
          }
        }
        // update uninstall.log
        unLogFile = fopen(uninstallFileName,"a+");
        if (unLogFile)
        {
          for (int i = 0; i < uninstallLog.Size(); i++)
          {
            if (uninstallLog[i].IsEmpty()) continue;
            fprintf(unLogFile, "%s\n", cc_cast(uninstallLog[i]));
          }
          fclose(unLogFile);
        }
      }
    }
  }
}

int PatchingThreadFunction(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow, Ref<ConsoleWindowProgress> console);

static void ApplyCommands(ParamEntryPar versionInfo,HINSTANCE hInstance, HINSTANCE hPrevInstance, int nCmdShow, Ref<ConsoleWindowProgress> console, const char *dir = NULL)
{
  ConstParamEntryPtr commands = versionInfo.FindEntry("Commands");
  if (commands)
  {
    char patchFolder[MAX_PATH];
    GetModuleFileName(NULL, patchFolder, MAX_PATH);
    (*(strrchr(patchFolder, '\\')+1))='\0'; //cut to full path including backslash


    char oldDir[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, oldDir);
    if (dir && *dir!='\0')
    {
      SetCurrentDirectory(dir);
    }


    for (int i=0; i<commands->GetEntryCount(); i++)
    {
      ParamEntryVal command = commands->GetEntry(i);
      if (!command.IsClass()) continue;

      RString commandLine = command >> "commandLine";
      if (commandLine.IsEmpty()) continue;

      commandLine = patchFolder + commandLine; // always us full path, WD should changed by previous  executed  

      ParamEntryPtr patchInPatch = command.FindEntry("patchInPatch");
      if (patchInPatch && (bool)*patchInPatch) 
      {
        RString filename = commandLine;
        filename.Lower();
        const char * pos = strstr(filename,".exe");
        if (!pos)
        {
          LogF("only EXE file allowed in \"patchInPatch\"");
        }
        else
        {
          filename = filename.Mid(0,pos-filename+4);
          if (FileExist(filename))
          {
            PatchingThreadFunction(hInstance,hPrevInstance,commandLine.MutableData(),nCmdShow,console); 
          }
          else
          {
            LogF("file %s not found",cc_cast(filename));
          }
        }
      }
      else
      {
    STARTUPINFO startupInfo;
    startupInfo.cb=sizeof(startupInfo); 
    startupInfo.lpReserved=0;
    startupInfo.lpDesktop=NULL;
    startupInfo.lpTitle=NULL;
    startupInfo.dwFlags=0;
    startupInfo.cbReserved2=0; 
    startupInfo.lpReserved2=NULL; 

      ConstParamEntryPtr entry = command.FindEntry("directory");
      RString directory;
      if (entry) directory = *entry;

      PROCESS_INFORMATION pi;
      BOOL ok = CreateProcess(
        NULL, commandLine.MutableData(),
        NULL,NULL, // security
        TRUE, // inheritance
        0, // creation flags 
        NULL, // env
        directory.GetLength() > 0 ? cc_cast(directory) : NULL, // pointer to current directory name 
        &startupInfo, &pi);

      if (ok)
      {
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
      }
      else
      {
        LogF("CreateProcess failed, error 0x%x", GetLastError());
      }
    }
    }
    if (dir && *dir!='\0')
    {
    SetCurrentDirectory(oldDir);
  }
}
}

#include <Es/Threads/pothread.hpp>
struct PatchingThreadContext
{
  HINSTANCE hInstance;
  HINSTANCE hPrevInstance;
  LPSTR lpCmdLine;
  int nCmdShow;
  Ref<ConsoleWindowProgress> console;

  PatchingThreadContext(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLn, int nCmdSh, Ref<ConsoleWindowProgress> cons)
    : hInstance(hInst), hPrevInstance(hPrevInst), lpCmdLine(lpCmdLn), nCmdShow(nCmdSh), console(cons)
  {}
};

/// Update 8-bit CRC value using polynomial X^8 + X^5 + X^4 + 1
static void UpdateCRC8(unsigned char &crc, unsigned char src)
{
  static const int polyVal = 0x8C;

  for (int i=0; i<8; i++)
  {
    if ((crc ^ src) & 1) crc = (crc >> 1 ) ^ polyVal;
    else crc >>= 1;
    src >>= 1;
  }
}


static bool FindFilenameInModuleList(DWORD processId, const char *modname)
{
  HANDLE modlist=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,processId);
  MODULEENTRY32 mentry;
  int lastChr = strlen(modname);
  mentry.dwSize=sizeof(mentry);
  if (Module32First(modlist,&mentry))
    do
    {      
      if (_strnicmp(mentry.szExePath,modname,lastChr)==0) 
      {
        CloseHandle(modlist);
        return true;
      }
    }
    while (Module32Next(modlist,&mentry));
  return false;
}

static int CheckForRunningProcesses(AutoArray<RString> appNames)
{
  HANDLE snapshot=::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS|TH32CS_SNAPTHREAD,0);
  for (int i=0;i<appNames.Size();++i)
  {    
    PROCESSENTRY32 pentry;
    pentry.dwSize=sizeof(pentry);
    if (Process32First(snapshot,&pentry))  
      do
      {      
        if (FindFilenameInModuleList(pentry.th32ProcessID,appNames[i])) //we found process
        {
          CloseHandle(snapshot);
          return i;
        }
      }
      while (Process32Next(snapshot,&pentry));
  }
  CloseHandle(snapshot);
  return -1;
}

static void AssociateFileExtension(ParamEntryPar versionInfo, RString distribution,AutoArray<RString> &uninstallLog)
{
  // Extended file type associations
  ConstParamEntryPtr list = versionInfo.FindEntry("FileAssociations");
  if (!list || !list->IsClass()) return;

  for (int i=0; i<list->GetEntryCount(); i++)
  {
    ParamEntryVal cls = list->GetEntry(i);
    if (!cls.IsClass()) continue;
    RString extension = cls >> "extension";
    RString exeName = cls >> "exeName";
    RString fileTypeName = cls >> "registryClass";
    RString icon = cls >> "iconNumber";
    RString productName = cls >> "productName";
    int fileVersion = cls >> "fileVersion";

    // check the current association
    HKEY key;
    int oldFileVersion = fileVersion;
    bool ok = ::RegOpenKeyEx(HKEY_CLASSES_ROOT, extension, 0, KEY_READ, &key) == ERROR_SUCCESS;
    if (ok)
    {
      // the key already exist
      TCHAR buffer[1024];
      // try to use the current file type
      DWORD size = sizeof(buffer);
      DWORD type = REG_SZ;
      LONG result = ::RegQueryValueEx(key, NULL, 0, &type, (BYTE *)buffer, &size);  
      if (result == ERROR_SUCCESS) fileTypeName = buffer;
      else ok = false;
      // read the current file version
      size = sizeof(buffer);
      type = REG_DWORD;
      result = ::RegQueryValueEx(key, _T("fileVersion"), 0, &type, (BYTE *)buffer, &size);  
      if (result == ERROR_SUCCESS) oldFileVersion = *(int *)buffer;

      ::RegCloseKey(key);
    }
    // fix the extension association if needed
    if (!ok || fileVersion > oldFileVersion)
    {
      if (::RegCreateKey(HKEY_CLASSES_ROOT, extension, &key) == ERROR_SUCCESS)
      {
        // new association to the file type is needed
        if (!ok) ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)fileTypeName, (fileTypeName.GetLength() + 1) * sizeof(TCHAR));
        // current file version need to be updated
        ::RegSetValueEx(key, _T("fileVersion"), 0, REG_DWORD, (BYTE *)&fileVersion, sizeof(int));
        
        ::RegCloseKey(key);
      }
    }

    // the full path to the associated exe
    RString exePath = distribution + exeName;
    RString defaultIcon = exePath + "," + icon;

    // update the application table
    if (::RegCreateKey(HKEY_CLASSES_ROOT, extension + "\\AppsTable\\" + productName, &key) == ERROR_SUCCESS)
    {
      // store the associated exe and the file version
      ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)exePath, (exePath.GetLength() + 1) * sizeof(TCHAR));
      ::RegSetValueEx(key, _T("fileVersion"), 0, REG_DWORD, (BYTE *)&fileVersion, sizeof(int));
      ::RegSetValueEx(key, _T("defaultIcon"), 0, REG_SZ, (BYTE *)(LPCTSTR)defaultIcon, (defaultIcon.GetLength() + 1) * sizeof(TCHAR));

      ::RegCloseKey(key);
    }

    // update the association of exe to the file type
    if (fileVersion >= oldFileVersion)
    {
      RString regKeyCommand = fileTypeName + "\\shell\\open\\command";
      if (::RegCreateKey(HKEY_CLASSES_ROOT, regKeyCommand, &key) == ERROR_SUCCESS)
      {
        RString command = "\"" + exePath + "\" \"%1\"";
        ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)command, (command.GetLength() + 1)*sizeof(TCHAR));
        ::RegCloseKey(key);
      }
      RString regKeyDefaultIcon = fileTypeName + "\\DefaultIcon";
      if (::RegCreateKey(HKEY_CLASSES_ROOT, regKeyDefaultIcon, &key) == ERROR_SUCCESS)
      {
        ::RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE *)(LPCTSTR)defaultIcon, (defaultIcon.GetLength()+1)*sizeof(TCHAR));
        ::RegCloseKey(key);
      }
    }

    // uninstall info
    uninstallLog.Add("A:"+extension + ":" + productName);
  }
}

int PatchingThreadFunctionPatch(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow, Ref<ConsoleWindowProgress> console, ParamEntryPar versionInfo,ExeContext ctx,bool isPatch, RString versionPatch,RString addonName,bool reportNotInstaled = true, RString defaultMultipatchDistribution = RString())
{
    RString configApp;
    RString appName;
    RString displayName;
    RString targetDir;

    char * lpCmdLineLow = strdup(lpCmdLine);
    strlwr(lpCmdLineLow);

  bool silent = false;
  bool inCurrentfolder = false;
  bool inWorkingfolder = false;
  if (strstr(lpCmdLineLow,"-silent")!=0) silent = true;
  if (strstr(lpCmdLineLow,"-incurrentfolder")!=0) inCurrentfolder = true;
  if (strstr(lpCmdLineLow,"-inworkingfolder")!=0) inWorkingfolder = true;


#ifndef _SUPER_RELEASE
    configApp = versionInfo >> "configApp";

    if (versionInfo.FindEntry("targetDirectory")) targetDir = versionInfo >> "targetDirectory";
    // the application name for user messages
    displayName = versionInfo >> "displayName";
    {
      RString versionPatch = versionInfo >> "version";
      PatchVersionTo = versionPatch;
      LANGID systemID = GetSystemDefaultUILanguage();
      LANGID userID = GetUserDefaultUILanguage();
      Log("GetSystemDefaultUILanguage = %d  GetUseDefaultUILanguage=%d", systemID, userID); //0x0409 English (United States) 
      WORD wPrimaryLangID = PRIMARYLANGID(GetUserDefaultLCID());
      WORD wDunLangID = SUBLANGID(GetUserDefaultLCID());      
      Log("GetUserDefaultLCID = %d  GetUserDefaultLCID = %d", wPrimaryLangID, wDunLangID);
/*
      //this experiment did not help
      LANGID wLangID;
      DWORD lcid;
      switch(wPrimaryLangID)
      {
      case LANG_GERMAN:
        wLangID = MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      case LANG_FRENCH:
        wLangID = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      case LANG_ITALIAN:
        wLangID = MAKELANGID(LANG_ITALIAN, SUBLANG_ITALIAN);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      case LANG_SPANISH:
        wLangID = MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      case LANG_CZECH:
        wLangID = MAKELANGID(LANG_CZECH, SUBLANG_DEFAULT);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      default:
        wLangID = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
        lcid = MAKELCID(wLangID, SORT_DEFAULT);
        SetThreadLocale(lcid);
        break;
      }
*/
    }
    if (displayName.GetLength() > 0) AppName = displayName;
#elif _VBS1
  configApp = "Software\\BIS\\VBS1";
#else
  configApp = "SOFTWARE\\Codemasters\\Operation Flashpoint";
#endif

  // retrieve application placement from registry
  HKEY key;
  char dir[MAX_PATH];
  DWORD dirSize = sizeof(dir) - 1; // '\' can be added
  bool installed = false;

#ifdef _VBS2 //we don't read the registry, get patch.exe dir instead
  if (true)
#else
  if (inWorkingfolder)
  {
    GetCurrentDirectory(MAX_PATH, dir);
    LogF("Dir: %s", dir);
    installed = true;
  }
  else if (inCurrentfolder)
#endif   
  {
  char exeName[MAX_PATH];
  GetModuleFileName(NULL, exeName, MAX_PATH);
  char *ext = strrchr(exeName, '\\');
  strncpy(dir, exeName, ext - exeName);
  LogF("Dir: %s", dir);
  installed = true;
  }
  else
  {
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, configApp, 0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    installed = RegQueryValueEx(key, "MAIN", NULL, NULL, (BYTE *)dir, &dirSize) == ERROR_SUCCESS;
    RegCloseKey(key);
  }
  }
  if (!installed)
  {
    // check steam and if arma exist in steam and is not in path by win registers patch
    RString steamApp = versionInfo >> "steamApp";
    RString steamRegKey = versionInfo >> "steamRegKey";
    RString steamToArmaPath =  versionInfo >> "steamToArmaPath";

    if (!steamApp.IsEmpty() && !steamRegKey.IsEmpty() && !steamToArmaPath.IsEmpty())
    {
      if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, steamApp , 0, KEY_READ, &key) == ERROR_SUCCESS)
      {
        installed = RegQueryValueEx(key, steamRegKey , NULL, NULL, (BYTE *)dir, &dirSize) == ERROR_SUCCESS;
        RegCloseKey(key);
        if (installed)
        {
          if (dir[strlen(dir) - 1] != '\\') strcat(dir, "\\");
          if (steamToArmaPath[0]=='\\') steamToArmaPath= steamToArmaPath.Mid(1);
          strcat(dir, cc_cast(steamToArmaPath));
          if (dir[strlen(dir) - 1] != '\\') strcat(dir, "\\");

          installed = false;
          ParamEntryVal appNameEntry = versionInfo.FindEntry("appName");
          if (appNameEntry.IsArray())
          {
            for (int i=0; i<appNameEntry.GetSize(); i++)
            {
              char app[256];
              RString fileName = appNameEntry[i].GetValue();
              if (!targetDir.IsEmpty())
              { //test files in target dir first
                strcpy(app, dir);
                strcat(app, targetDir);
                strcat(app, "\\");
                strcat(app, fileName);
                if (FileExist(app)) installed=true;
              }
              strcpy(app, dir);
              strcat(app, fileName);
              if (FileExist(app)) installed=true;
            }
          }
          else
          {
            appName = appNameEntry.GetValue();
            char app[256];
            if (!targetDir.IsEmpty())
            { //test file in target dir first
              strcpy(app, dir);
              strcat(app, targetDir);
              strcat(app, "\\");
              strcat(app, appName);
              if (FileExist(app)) installed=true;
            }
            strcpy(app, dir);
            strcat(app, appName);
            if (FileExist(app)) installed=true;
          }
        }
      }
    }
    if (!installed)
    {
    char buffer[256];
    LoadAppString(hInstance, IDS_NOT_INSTALLED, buffer, sizeof(buffer));
      if (!silent && reportNotInstaled) ErrorMessage(hInstance, buffer);
    return 1;
  }
  }
  if (dir[strlen(dir) - 1] != '\\') strcat(dir, "\\");

#ifndef _SUPER_RELEASE
  AutoArray<RString> appNames;
  ParamEntryVal appNameEntry = versionInfo.FindEntry("appName");
  // test for patch with no exe in them
  if (appNameEntry.GetPointer())
  {
  if (appNameEntry.IsArray())
  {
    for (int i=0; i<appNameEntry.GetSize(); i++)
    {
      char app[256];
      RString fileName = appNameEntry[i].GetValue();
      if (!targetDir.IsEmpty())
      { //test files in target dir first
        strcpy(app, dir);
        strcat(app, targetDir);
        strcat(app, "\\");
        strcat(app, fileName);
        if (FileExist(app)) appNames.Add(app);
      }
      strcpy(app, dir);
      strcat(app, fileName);
      if (FileExist(app)) appNames.Add(app);
    }
  }
  else
  {
    appName = appNameEntry.GetValue();
    char app[256];
    if (!targetDir.IsEmpty())
    { //test file in target dir first
      strcpy(app, dir);
      strcat(app, targetDir);
      strcat(app, "\\");
      strcat(app, appName);
      if (FileExist(app)) appNames.Add(app);
    }
    strcpy(app, dir);
    strcat(app, appName);
    if (FileExist(app)) appNames.Add(app);
  }
  if (appNames.Size()>0)
  {
    appName = appNames[0];
  }
  else
  {
    char buffer[1024];
    LoadAppString(hInstance, IDS_NOT_INSTALLED, buffer, sizeof(buffer));
    if (!silent) ErrorMessage(hInstance, buffer);
    return 1;
  }
  }

#if _ENABLE_PBO_PROTECTION
  ParamEntryPtr parentConfigApp = versionInfo.FindEntry("parentConfigApp");
  if (parentConfigApp.NotNull())
  {
    if (parentConfigApp->IsTextValue())
    {
      PBOHeaderProtection::AppRegistryPath = *parentConfigApp; 
    }
    else if (parentConfigApp->IsArray())
    {
      bool regFound = false;
      for (int i = 0; i < parentConfigApp->GetSize();++i)
      {
        RString parentReg = (*parentConfigApp)[i];
        PBOHeaderProtection::AppRegistryPath = parentReg;

        ParamEntryVal multiParentTest = versionInfo >> "testParentRegOnAddon";

        bool succes = true;
        for (int j = 0; j < multiParentTest.GetSize();++j)
        {
          RString addonName = multiParentTest[j];

          QFBank addon;
          if (!addon.openFromFile(dir+addonName))
          {
            ErrorMessage(hInstance, Format("Patch file is corrupted, file %s not found",cc_cast(addonName)));
            return 1;
          }  

          RString regSerialProt = addon.GetProperty(PBOHeaderProtection::PRORegSerialProp);
          if (regSerialProt.IsEmpty())
          {
            ErrorMessage(hInstance, Format("Failed to get serial numbers for header decryption in %s",cc_cast(addonName)));
            return 1;
          }

          PBOHeaderProtection::TByteBuffer serial1,serial2;
          if (!PBOHeaderProtection::CreateProtectionSerials(parentReg, regSerialProt, serial1, serial2))
          {
            succes = false; break;
          }

          if (!addon.Load()) // test if we can open PBO in  
          {
            succes = false; break;
          }
        }
        if (succes) 
        {
          regFound = true ; break; // we found right key ant its already set in AppRegistryPath
        }
      }
      if (!regFound && !silent)
      {
        ErrorMessage(hInstance,Format("Unable to patch %s, registry key is not valid",cc_cast(displayName)));
        return 1;
      }
    }
  }
  else
    PBOHeaderProtection::AppRegistryPath = configApp;
#endif


  // check CD keys
  ConstParamEntryPtr list = versionInfo.FindEntry("CDKeys");
  if (list)
  {
    for (int i=0; i<list->GetEntryCount(); i++)
    {
      ParamEntryVal cls = list->GetEntry(i);
      if (!cls.IsClass()) continue;

// console->DebugF("Testing CD Key: %s", cc_cast(cls.GetName()));

      bool registryUser = cls >> "registryUser";
      RString registryPath = cls >> "registryPath";

      // Read the CD Key from registry
      static const int publicKeySize = KEY_BYTES;
      unsigned char cdKey[KEY_BYTES];

      HKEY rootKey = registryUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;

      if (::RegOpenKey(rootKey, registryPath, &key) != ERROR_SUCCESS)
      {
        char buffer[1024];
        LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
        ErrorMessage(hInstance, buffer);
        return 1;
      }

      DWORD cdKeyLen = publicKeySize;
      bool ok = ::RegQueryValueEx(key, "KEY", 0, NULL, cdKey, &cdKeyLen) == ERROR_SUCCESS;
      RegCloseKey(key);
      if (!ok)
      {
        char buffer[1024];
        LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
        ErrorMessage(hInstance, buffer);
        return 1;
      }

      int version = 1;
      AutoArray<int> productID;
      ConstParamEntryPtr ver = cls.FindEntry("version");
      if (ver) version = *ver;
      ConstParamEntryPtr pID = cls.FindEntry("productID");

      if (!pID) 
        productID.Add(0); // set default product 0 = ARMA2
      else
      {
        if (pID->IsArray()) // DLC BAF is product 2 (online) or 5 (reinforcements setup)
        {
          for (int j = 0; j < pID->GetSize();++j)
            productID.Add((*pID)[j]);
        }
        else
          productID.Add(*pID);
      }
// console->DebugF(" - version = %d", version);

      __int64 val;
      if (version >= 2)
      {
        // Key version 2 used for ArmA 2
        // CRC check
        unsigned char crc = 0;
        for (int i=0; i<KEY_BYTES-1; i++) UpdateCRC8(crc, cdKey[i]);

        bool match = false;
        for (int j = 0; j< productID.Size();++j)
        {
          if ((crc^productID[j]) == cdKey[KEY_BYTES - 1])
          {
            match = true;
            break;
          }
        }
          
        if (!match)
        {
          char buffer[1024];
          LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
          ErrorMessage(hInstance, buffer);
          return 1;
        }
        // player id
        val = *(unsigned int *)cdKey;
// console->DebugF(" - player = %u", val);
      }
      else
      {
        // Public RSA key
        ParamEntryVal array = cls >> "publicKey";
        if (!array.IsArray() || array.GetSize() != publicKeySize + 4) continue;
        unsigned char publicKey[publicKeySize + 4];
        for (int c=0; c<publicKeySize+4; c++) publicKey[c] = (int)array[c];

        RString fixed = cls >> "fixed";
        int idLen = publicKeySize - fixed.GetLength();

        CDKey cdk;
        cdk.Init(cdKey, publicKey);
        if (!cdk.Check(idLen, fixed))
        {
          char buffer[1024];
          LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
          ErrorMessage(hInstance, buffer);
          return 1;
        }
        val = cdk.GetValue(0, idLen);
      }

      ParamEntryVal array = cls >> "unsupported";
      for (int j=0; j<array.GetSize(); j++)
      {
        RString value = array[j];
        __int64 unsupported = _atoi64(value);
// console->DebugF(" - comparing with unsupported %I64d %I64d", val, unsupported);
        if (val == unsupported)
        {
          char buffer[1024];
          LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
          ErrorMessage(hInstance, buffer);
          return 1;
        }
      }
    }
  }

#else // #ifndef _SUPER_RELEASE

  static const struct
  {
    const char *_appName;
    unsigned char _publicKey[19];
    int _checkOffset;
    const char *_checkWith;
  } superInfo[] =
  {
#if _VBS1
    {
      "VBS1.exe",
      { 0x11, 0x00, 0x00, 0x00, 0xF9, 0x54, 0x9E, 0x33, 0x73, 0xCD, 0x0F, 0x5A, 0x15, 0x85, 0xE2, 0xAC, 0x32, 0x80, 0xA1, },
      5,
      "VBSystem 1",
    },
#else
    {
      "FlashpointResistance.exe",
      { 0x13, 0x00, 0x00, 0x00, 0xF7, 0xB0, 0x64, 0xD7, 0xDB, 0x87, 0x52, 0x27, 0x57, 0x91, 0xB1, 0x9E, 0x0E, 0x81, 0x88, },
      5,
      "Flashpoint",
    },
    {
      "OperationFlashpoint.exe",
      { 0x13, 0x00, 0x00, 0x00, 0xF7, 0xB0, 0x64, 0xD7, 0xDB, 0x87, 0x52, 0x27, 0x57, 0x91, 0xB1, 0x9E, 0x0E, 0x81, 0x88, },
      5,
      "Flashpoint",
      },
#endif
      {
        NULL,
        { 0x0, },
        0,
        NULL,
      },
  };

  char app[256];
  int i = 0;
  while (true)
  {
    if (superInfo[i]._appName == NULL)
    {
      char buffer[1024];
      LoadAppString(hInstance, IDS_NOT_INSTALLED, buffer, sizeof(buffer));
      if (!silent) ErrorMessage(hInstance, buffer);
      return 1;
    }

    strcpy (appName, superInfo[i]._appName);
    strcpy(app, dir);
    strcat(app, appName);
    if (FileExist(app))
    {
      break;
    }

    ++i;
  }

  // Check CD-Key
  DWORD cdKeyFromRegistryLength = KEY_BYTES;
  unsigned char cdKeyFromRegistry[KEY_BYTES];
  bool cdKeyFromRegistryValid;

#if _VBS1
  cdKeyFromRegistryValid = ::RegOpenKey (HKEY_LOCAL_MACHINE, "Software\\BIS\\VBS1", &key) == ERROR_SUCCESS;
#else
  cdKeyFromRegistryValid = ::RegOpenKey (HKEY_LOCAL_MACHINE, "SOFTWARE\\Codemasters\\Operation Flashpoint", &key) == ERROR_SUCCESS;
#endif
  if (!cdKeyFromRegistryValid)
  {
    char buffer[1024];
    LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
    ErrorMessage(hInstance, buffer);
    return 1;
  }
  cdKeyFromRegistryValid = ::RegQueryValueEx (key, "KEY", 0, NULL, (BYTE*) (&cdKeyFromRegistry[0]), &cdKeyFromRegistryLength) == ERROR_SUCCESS;
  RegCloseKey (key);
  if (!cdKeyFromRegistryValid)
  {
    char buffer[1024];
    LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
    ErrorMessage(hInstance, buffer);
    return 1;
  }

  CDKey cdk;
  cdk.Init(cdKeyFromRegistry, superInfo[i]._publicKey);
  if (!cdk.Check(superInfo[i]._checkOffset, superInfo[i]._checkWith))
  {
    char buffer[1024];
    LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
    ErrorMessage(hInstance, buffer);
    return 1;
  }

#if !_VBS1
  struct
  {
    __int64 _min;
    __int64 _max;
  } validIntervals[] =
  {
    { (__int64)    1, (__int64)  1E7 },
    { (__int64) 10E7, (__int64) 11E7 },
    { (__int64)    1, (__int64)    0 },
  };
  __int64 val = cdk.GetValue(0, 5);
  i = 0;
  while (true)
  {
    if (validIntervals[i]._min > validIntervals[i]._max)
    {
      char buffer[1024];
      LoadAppString(hInstance, IDS_BAD_CDKEY, buffer, sizeof(buffer));
      ErrorMessage(hInstance, buffer);
      return 1;
    }
    if (val >= validIntervals[i]._min && val <= validIntervals[i]._max)
    {
      break;
    }
    ++i;
  }
#endif // _VBS1

#endif // #ifndef _SUPER_RELEASE #else

  // retrieve application version
  char version[256];
  if (appNames.Size()==0)
  {
    // for patch with no exe in them
    strcpy(version,PatchVersionTo);
  }
  else
  {
  GetAppVersion(appNames, version);
  if (!*version)
  {
    char buffer[1024];
    LoadAppString(hInstance, IDS_NOT_INSTALLED, buffer, sizeof(buffer));
    if (!silent) ErrorMessage(hInstance, buffer);
    return 1;
  }
  }


#if _DEBUG
  //HMODULE hmod = GetModuleHandle(NULL);
  //EnumResourceTypes(hmod,EnumResTypes,NULL);

  EnumResourceNames(NULL,RES_TYPE,EnumResNames,NULL);

#endif


  int versionPatchNum = ScanVersion(versionPatch);
  int versionNum = ScanVersion(version);


  ParamEntryPtr entry = versionInfo.FindEntry("requiredForAddons");
  RString helper = entry ? entry->GetValue() : RString();
  int versionRequiredForAddons = ScanVersion(helper);

  entry = versionInfo.FindEntry("requiredForPatch");
  helper = entry ? entry->GetValue() : RString();
  int versionRequiredForPatch = ScanVersion(helper);

  bool isAddon = addonName.GetLength() > 0; // additonal units present?

  
  RString distribution = dir+targetDir;
  AssociateFileExtension(versionInfo,distribution,GUninstalLog);
  int checkVal = CheckForRunningProcesses(appNames);
  if (checkVal>-1)    
  {
    ErrorMessage(hInstance, "Couldn't apply patch, because " + appNames[checkVal]+" is running.");
    return 1;
  };

  bool sameVersion = false;
  if (!isPatch)
  {
    bool sameVersion = true;
    // addon version verification
    // exe version must be at least addon version required
    if (versionRequiredForAddons>versionNum)
    {
      char buffer[1024];
      LoadAppString(hInstance, IDS_REQ_VERSION, buffer, sizeof(buffer));
      char message[1024];
      sprintf
        (
        message, buffer,
        versionRequiredForAddons/100, versionRequiredForAddons%100,
        versionNum/100, versionNum%100
        );
      ErrorMessage(hInstance, message);
      return 1;
    }
  }
  else
  {
    if (versionRequiredForPatch>0 && versionRequiredForPatch>versionNum)
    {
      char buffer[1024];
      LoadAppString(hInstance, IDS_REQ_VERSION, buffer, sizeof(buffer));
      char message[1024];
      sprintf
        (
        message, buffer,
        versionRequiredForPatch/100, versionRequiredForPatch%100,
        versionNum/100, versionNum%100
        );
      ErrorMessage(hInstance, message);
      return 1;
    }
    // we will increase version as necessary
    if (!strcmpi(versionPatch,version))
    {
      sameVersion = true;
    }
    else if(strcmp(versionPatch,version)<=0)
    {
      char buffer[1024];
      LoadAppString(hInstance, IDS_MORE_RECENT, buffer, sizeof(buffer));
      char message[1024];
      sprintf(message, buffer, version, cc_cast(versionPatch));
      ErrorMessage(hInstance, message);
      return 1;
    }
  }

  // load the list of distributions
  AutoArray<DistributionInfo> distributions;
  FindArrayRStringCI currentDistribution; // which distribution we can be
  LoadDistributions(versionInfo, distributions, currentDistribution);

  // try to find the current distribution
  if (currentDistribution.Size() > 0)
    FindDistribution(versionInfo, dir, currentDistribution);

  AutoArray<RString> uninstallLogName;
  ParamEntryPtr unistl = versionInfo.FindEntry("uninstallLog");
  if (unistl) 
  {
    if (unistl->IsArray())
    {
      for (int i =0; i<unistl->GetSize();i++)
        uninstallLogName.Add((*unistl)[i]);
    }
    else
    {
      uninstallLogName.Add((RString)(*unistl));
    }
  }
  else
  {
    uninstallLogName.Add("UnInstall.log"); 
  }
  // do not patch steam version of the ArmA
  bool doNotInstallIntoSteam = false;
  if (versionInfo.FindEntry("doNotInstallIntoSteam")) 
    doNotInstallIntoSteam = versionInfo >> "doNotInstallIntoSteam";

  if (doNotInstallIntoSteam)
  {
    _finddata_t data;
    char vdf[MAX_PATH];
    strcpy(vdf, dir);
    strcat(vdf, targetDir);
    MakeLongDirStr(vdf,vdf);
    strcat(vdf, "*.vdf");
    long h = _findfirst(vdf,&data);
    if (h!=-1)  
    {
      MessageBox(NULL,"This patch will not work with Steam version of the game.","Patch error",MB_OK|MB_ICONERROR);
      return 1;
    }
  }

  // Show Intro Message Box saying the basic info about installation
  // TODO: localization
  char introText[2048];
  char introQuestion[2048];
  LoadAppString(hInstance, IDS_INTRO_MESSAGE, introText, sizeof(introText));
  LoadAppString(hInstance, IDS_INTRO_QUESTION, introQuestion, sizeof(introQuestion));
  RString introMsg = Format("%s\n%s", introText, introQuestion);
  char caption[256];
  LoadAppString(hInstance, IDS_CAPTION, caption, sizeof(caption));
  if (!silent && MessageBox(NULL, cc_cast(introMsg), caption, MB_YESNO | MB_ICONQUESTION)==IDNO)
  { //abort installation
    return 10;
  }

  RString forceDistribution;
  if (versionInfo.FindEntry("forceDistribution")) 
    forceDistribution = versionInfo >> "forceDistribution";

  if (currentDistribution.Size() > 1)
  {
    if (!forceDistribution.IsEmpty())
    {
      currentDistribution.Clear();
      currentDistribution.Add(forceDistribution);
    }
    else
    {
      // let the user select the distribution
    SelectDistributionDialog dlg(hInstance, distributions, currentDistribution);
    RString selected = dlg.GetResult();
    currentDistribution.Clear();
    if (selected.GetLength() == 0) return 1; // cancelled
    currentDistribution.Add(selected);
  }
  }

  // add parent distributions
  if (currentDistribution.Size() > 0)
  {
    Assert(currentDistribution.Size() == 1);
    int index = -1;
    for (int i=0; i<distributions.Size(); i++)
    {
      if (stricmp(currentDistribution[0], distributions[i].id) == 0)
      {
        index = distributions[i].parentIndex;
        if (!distributions[i].name.IsEmpty()) console->PrintF(IDS_INFO_TEXT, cc_cast(distributions[i].name));
        break;
      }
    }
    while (index >= 0)
    {
      currentDistribution.Add(distributions[index].id);
      index = distributions[index].parentIndex;
    }
  }

  // also allow multipatch entry name as a distribution 
  if (!defaultMultipatchDistribution.IsEmpty())
    currentDistribution.Add(defaultMultipatchDistribution);

  int verMajor = versionNum/100, verMinor = versionNum%100;
  int verMajorPatch = versionPatchNum/100, verMinorPatch= versionPatchNum%100;
  Log("Version original: %d.%02d",verMajor,verMinor);
  Log("Version applied: %d.%02d",verMajorPatch,verMinorPatch);

  // show the progress dlg window! (which is already running in the main thread)
  console->ShowWindow(true);
  
  // check space required
  EnumApplyContext context;
  context.found = false;
  context.error = false;
  context.console = console;
  context.verMajor = verMajor;
  context.verMinor = verMinor;
  context.verMajorPatch = verMajorPatch;
  context.verMinorPatch = verMinorPatch;
  context.ctx = &ctx;
  context.distribution = currentDistribution.Size() > 0 ? currentDistribution[0] : RString();
  context.targetDir = targetDir;

#define BETA 0

#if BETA
  console->SetTitle(IDS_UPGRADE_CAPTION2);
#else
  if (addonName.GetLength() > 0)
  {
    console->SetTitle(IDS_UPGRADE_CAPTION, cc_cast(addonName));
  }
  else
  {
    if (stricmp(version,cc_cast(versionPatch))==0)
      console->SetTitle(IDS_UPDATE_CAPTION2, version);
    else
    console->SetTitle(IDS_UPDATE_CAPTION, version, cc_cast(versionPatch));
  }
#endif

  context.console->InitProgressBar(0); //nothing patched yet
  context.console->SetProgressBar(0);  //but show slow progress (4% at start - keep optimistic mood)


  if (true) //apply diffs from "DFF" folder, recognized by CRC
  {
#if USE_DELTA3_BINARY_DIFF
    char XDelta3TmpFolder[MAX_PATH];
    if (GetTempPath(MAX_PATH, XDelta3TmpFolder) == 0)
    {
      ApplyError(context.console,5,"Temporary directory");
      return 1;
    }
    strcat(XDelta3TmpFolder, "BIPatch");
    strcpy(XDelta3TmpFile,XDelta3TmpFolder);
    strcat(XDelta3TmpFile,"\\xdelta3.exe");
    if (!ResourceToFile("XDELTA3.EXE",XDelta3TmpFile,context.ctx))
    {
      ApplyError(context.console,5,"cannot find xdelta3.exe");
      return 1;
    }
#endif
    int minor;
    __int64 patchBytesCount = 0i64;

    minor = verMinor;
    strcpy(context.dir,dir);
    // scan resources for incremental updates from this version
    sprintf(context.version,"DFF",verMajor,minor);

    // collect the files we will apply
    context.files.Resize(0);
    EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
    FilterByDistributions(context.files, currentDistribution, true);

    // check if there is enough space for application of each update
    if (!IncrementalCheckSpace(context, patchBytesCount))
    {
      //console->Destroy();
      char buffer[1024];
      LoadAppString(hInstance, IDS_DISK_LOW, buffer, sizeof(buffer));
      char message[1024];
      char diskname[32];
      sprintf(diskname,"%c:",dir[0]);
      sprintf(message, buffer, diskname);
      ErrorMessage(hInstance, message);
      return 1;
    }
    //ProcessPatchMessages();
    //init progress bar settings
    context.console->InitProgressBar(patchBytesCount);

    EnumApplyContext applyContext = context;
    applyContext.files.Resize(1);


  FilterByCRC(context.files);

  for (int i = 0; i< context.files.Size();i++)
  {
    RString name = context.files[i].Mid(strlen(context.version)+1);
    
    bool found = false;
    HRSRC hRsc;
    do
    {
        unsigned int crcFile = ComputeCRC(context.dir+name);

      char resName[512];
      sprintf(resName,"%s.!%08X",cc_cast(context.files[i]),crcFile);
      hRsc = FindResource(context.ctx->hMod, resName, RES_TYPE);
      if (hRsc)
      {
        applyContext.files[0]=resName;
        found = IncrementalPatchApply(applyContext,true);
      }
      else
      {
        found = false;
      }
    }
    while (found);
  }


#if USE_DELTA3_BINARY_DIFF    
    DeleteDirectoryStructure(XDelta3TmpFolder);
#endif
    if (applyContext.error)
    {
      ApplyError(applyContext.console,1,applyContext.errorFile);
      return 1;
    }
  }


  if (sameVersion && isPatch)
  {
    bool forceInstallAllFolder = false;
    if (versionInfo.FindEntry("forceInstallAllFolder")) 
      forceInstallAllFolder = versionInfo >> "forceInstallAllFolder";
    if (forceInstallAllFolder)
    {
      // version specific copy file
      strcpy(context.dir,dir);
      strcpy(context.version,version);
      strcat(context.version, "\\");
      context.files.Resize(0);

      EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
      FilterByDistributions(context.files, currentDistribution);
      ApplyPatch(context);

      // All folder stuff
      strcpy(context.version,"All");
      strcat(context.version, "\\");
      context.files.Resize(0);

      EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
      FilterByDistributions(context.files, currentDistribution);
      ApplyPatch(context);
    }
    console->PrintF(IDS_VERIFY_CAPTION,verMajorPatch,verMinorPatch);
    strcpy(context.dir,dir);
    strcpy(context.version,version);
    strcat(context.version, "\\");
    CheckVersionResult cvr;
    if (context.targetDir.IsEmpty())
      cvr = CheckVersion(context, &currentDistribution, NULL, false);
    else
    { //we need to change context.dir to targetDir subdirectory
      RString contextDirBackup = context.dir;
      strcat(context.dir, cc_cast(context.targetDir));
      strcat(context.dir, "\\");
      cvr = CheckVersion(context, &currentDistribution, NULL, false);
      strcpy(context.dir, cc_cast(contextDirBackup));
    }
    bool ok = cvr == CVROK;
    if (ok)
    {
      if (forceInstallAllFolder)
        console->PrintF(IDS_PATCHED_OK, cc_cast(versionPatch), version);
      else
        console->PrintF(IDS_ALREADY_PATCHED,version);      
    }
    else
    {
      console->PrintF(IDS_UPGRADE_FAILED,version,10);
    }
    
    if (context.found)
    {
      // the registry should be updated for sameVersions too (DLC content)
      UpdateRegistry(versionInfo, context.distribution);
    }

    console->SetWaitForClosing();

#if PARSE_RESOURCE_OURSELVES
    UnmapViewOfFile(base);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
#endif
    if (forceInstallAllFolder)
    {
      // execute commands
      ApplyCommands(versionInfo, hInstance, hPrevInstance, nCmdShow, console, context.dir);
      //create shortcut, if specified in version.cfg
      ApplyShortcuts(console->GetHWnd(), versionInfo, dir, GUninstalLog, context.distribution);
      //update UNINSTALL 
      ApplyUninstall(GUninstalLog, context.dir,uninstallLogName);
    }
    console->SetProgress("");
    console->StopAnimation();

    return ok ? 0 : 1;
  }

  if (!sameVersion && !isAddon)
  {
#if 0
    console->PrintF(IDS_VERIFY_CAPTION,verMajor,verMinor);

    strcpy(context.dir,dir);
    strcpy(context.version,version);
    strcat(context.version, "\\");

    CheckVersionResult cvr = CheckVersion(context, &currentDistribution);
    if (cvr!=CVROK)
    {
      console->PrintF(IDS_BAD_SOURCE,version);
      console->SetWaitForClosing();
      return 1;
    }
#endif
  }

  // set update caption
#if BETA
  console->PrintF(IDS_UPGRADE_CAPTION2);
#else
  if (addonName.GetLength() > 0)
  {
    console->PrintF(IDS_UPGRADE_CAPTION, cc_cast(addonName));
  }
  else
  {
    console->PrintF(IDS_UPDATE_CAPTION, version, cc_cast(versionPatch));
  }
#endif

  if (isPatch)
  {
#if USE_DELTA3_BINARY_DIFF
    char XDelta3TmpFolder[MAX_PATH];
    if (GetTempPath(MAX_PATH, XDelta3TmpFolder) == 0)
    {
      ApplyError(context.console,5,"Temporary directory");
      return 1;
    }
    strcat(XDelta3TmpFolder, "BIPatch");
    strcpy(XDelta3TmpFile,XDelta3TmpFolder);
    strcat(XDelta3TmpFile,"\\xdelta3.exe");
    if (!ResourceToFile("XDELTA3.EXE",XDelta3TmpFile,context.ctx))
    {
      ApplyError(context.console,5,"cannot find xdelta3.exe");
      return 1;
    }
#endif
    int minor;
    __int64 patchBytesCount = 0i64;
    for (minor = verMinor; minor<verMinorPatch; minor++)
    {
      strcpy(context.dir,dir);
      // scan resources for incremental updates from this version
      sprintf(context.version,"%d.%02d-",verMajor,minor);

      // collect the files we will apply
      context.files.Resize(0);
      EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
      FilterByDistributions(context.files, currentDistribution, true);

      // check if there is enough space for application of each update
      if (!IncrementalCheckSpace(context, patchBytesCount))
      {
        //console->Destroy();
        char buffer[1024];
        LoadAppString(hInstance, IDS_DISK_LOW, buffer, sizeof(buffer));
        char message[1024];
        char diskname[32];
        sprintf(diskname,"%c:",dir[0]);
        sprintf(message, buffer, diskname);
        ErrorMessage(hInstance, message);
        return 1;
      }
      //ProcessPatchMessages();
    }
    //init progress bar settings
    context.console->InitProgressBar(patchBytesCount);

    // first apply version specific information (incremental)
    for (minor = verMinor; minor<verMinorPatch; minor++)
    {
      strcpy(context.dir,dir);
      // scan resources for incremental updates from this version
      sprintf(context.version,"%d.%02d-",verMajor,minor);

      // collect the files we will apply
      context.files.Resize(0);
      EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
      FilterByDistributions(context.files, currentDistribution, true);

      if (!IncrementalPatchApply(context)) break;
    }
#if USE_DELTA3_BINARY_DIFF    
    DeleteDirectoryStructure(XDelta3TmpFolder);
#endif
    if (context.error)
    {
      //console->Destroy();
      ApplyError(context.console,1,context.errorFile);
      return 1;
    }
  }

  // version specific copy file
  strcpy(context.dir,dir);
  strcpy(context.version,version);
  strcat(context.version, "\\");
  context.files.Resize(0);

  EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
  FilterByDistributions(context.files, currentDistribution);
  ApplyPatch(context);
  //ProcessPatchMessages();

  if (context.error)
  {
    ApplyError(context.console,2,context.errorFile);
    return 1;
  }

  // then apply information common for all versions (copy files)
  if (isPatch)
  {
    strcpy(context.version,"All");
    strcat(context.version, "\\");
    context.files.Resize(0);

    EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
    FilterByDistributions(context.files, currentDistribution);
    ApplyPatch(context);
    //ProcessPatchMessages();

    if (context.error)
    {
      ApplyError(context.console,3,context.errorFile);
      return 1;
    }

    // commands to execute:
    // - copy needed files to the temporary directory
    if (GetTempPath(MAX_PATH, context.dir) == 0)
    {
      ApplyError(context.console,5,"Temporary directory");
      return 1;
    }
    strcat(context.dir, "BIPatch\\");
    strcpy(context.version,"Tmp\\");
    context.files.Resize(0);

    EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
    FilterByDistributions(context.files, currentDistribution);
    ApplyPatch(context);
    //ProcessPatchMessages();

    if (context.error)
    {
      ApplyError(context.console,5,context.errorFile);
      return 1;
    }

    // - execute the commands
    ApplyCommands(versionInfo,hInstance, hPrevInstance, nCmdShow, console, dir);

    // - remove the temporary directory again
    context.dir[strlen(context.dir) - 1] = 0; // remove the trailing \ again
    DeleteDirectoryStructure(context.dir);
  }

  strcpy(context.dir,dir);
  strcpy(context.version,"Add");
  strcat(context.version, "\\");
  context.files.Resize(0);

  EnumResourceNamesC(ctx,RES_TYPE,EnumResNamesCollect,&context);
  FilterByDistributions(context.files, currentDistribution);
  ApplyPatch(context);
  //ProcessPatchMessages();

#if GOLD_UPGRADE
  CreateUninstall(dir);
#endif

  if (context.error)
  {
    ApplyError(context.console,4,context.errorFile);

#if PARSE_RESOURCE_OURSELVES
    UnmapViewOfFile(base);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
#endif

    return 1;
  }

  if (!context.found)
  {
    //console->Destroy();
    char buffer[1024];
    LoadAppString(hInstance, IDS_BAD_VERSION, buffer, sizeof(buffer));
    char message[1024];
    sprintf(message, buffer, version, cc_cast(versionPatch));
    ErrorMessage(hInstance, message);

#if PARSE_RESOURCE_OURSELVES
    UnmapViewOfFile(base);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
#endif

    return 1;
  }
  else
  {
    UpdateRegistry(versionInfo, context.distribution);

    //create shortcut, if specified in version.cfg
    ApplyShortcuts(console->GetHWnd(), versionInfo, dir, GUninstalLog, context.distribution);
    //update UNINSTALL 
    ApplyUninstall(GUninstalLog, context.dir,uninstallLogName);

    console->SetProgress("");
    console->StopAnimation();
    if (addonName.GetLength() > 0)
    {
      console->PrintF(IDS_UPGRADE_OK, cc_cast(addonName));
    }
    else
    {
      console->PrintF(IDS_PATCHED_OK, cc_cast(versionPatch), version);
    }

    console->SetWaitForClosing();

#if PARSE_RESOURCE_OURSELVES
    UnmapViewOfFile(base);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
#endif

    return 0;
  }
}

int PatchingThreadFunction(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow, Ref<ConsoleWindowProgress> console)
{
  // filename may be passed on the command line
  char thisNameBuf[1024];
  GetModuleFileName(NULL, thisNameBuf, sizeof(thisNameBuf));
  char *thisName = thisNameBuf;

  if (*lpCmdLine)
  {
    thisName = strdup(lpCmdLine);
    strlwr(thisName);
    char * pos = strstr(thisName,".exe");
    if (pos) *(pos+4) = '\0';
  }
#if PARSE_RESOURCE_OURSELVES

  HANDLE hFile;
  HANDLE hFileMapping;
  LPVOID base;

  hFile = CreateFile
    (
    thisName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0
    );

  if (hFile == INVALID_HANDLE_VALUE)
  {
    ErrorMessage(hInstance, "Couldn't open file with CreateFile()");
    return 1;
  }

  hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
  if (hFileMapping == 0)
  {
    CloseHandle(hFile);
    ErrorMessage(hInstance, "Couldn't open file mapping with CreateFileMapping()");
    return 1;
  }

  base = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
  DWORD ibase = (DWORD)base;
  if (base == 0)
  {
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
    ErrorMessage(hInstance, "Couldn't map view of file with MapViewOfFile()");
    return 1;
  }

  ResContext ctx;
  PIMAGE_RESOURCE_DIRECTORY resDir = GetResourceDirectory
    (
    hInstance, base, ctx.ntHeader
    );
  if (!resDir)
  {
    UnmapViewOfFile(base);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
    return 1;
  }

  ctx.base = (DWORD)base;
  ctx.ibase = ibase;
  ctx.resDir = resDir;
#else
  ExeContext ctx;
  ctx.hMod = LoadLibraryEx(thisName,NULL,LOAD_LIBRARY_AS_DATAFILE);
#endif

#ifndef _SUPER_RELEASE
  // read "version.cfg"
  int size;
  void *ptr = FindAndLoadResource(size, &ctx, "version.cfg");
  if (!ptr)
  {
    ErrorMessage(hInstance, "Patch file is corrupted, error 1.");
    return 1;
  }

  // check the signature of "version.cfg"
  static const unsigned char keyContent[] = {
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31, 0x00, 0x04, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x41, 0xB2, 0x6E, 0x5A, 0x85, 0x6B, 0x87, 0x93, 0x8F, 0x8C, 0xB9, 0x0C,
    0x34, 0x2D, 0x47, 0x58, 0xD5, 0x6C, 0x94, 0x33, 0xFA, 0xC2, 0xA1, 0x08, 0x59, 0x3A, 0xD4, 0x44,
    0xED, 0xFB, 0xA2, 0xDC, 0xB0, 0x7A, 0x3A, 0x9C, 0xE5, 0x93, 0x7C, 0xC1, 0x50, 0x49, 0xBB, 0x4D,
    0xED, 0x20, 0x65, 0xAB, 0xF8, 0xFE, 0xAD, 0xB2, 0xBB, 0xE2, 0x00, 0x54, 0x62, 0xB2, 0x96, 0x37,
    0x0E, 0x47, 0xBB, 0x7A, 0x35, 0x9E, 0xD4, 0x85, 0xC4, 0x76, 0x59, 0x92, 0xAF, 0x84, 0xF7, 0x5D,
    0xC1, 0xB3, 0x19, 0xFA, 0x8B, 0xF5, 0xFE, 0x96, 0xB9, 0x05, 0x99, 0xC6, 0x05, 0xC7, 0x8F, 0x6E,
    0x9F, 0x3A, 0xB0, 0xD1, 0x02, 0x3A, 0x8F, 0x3D, 0xA4, 0x08, 0x09, 0x4D, 0xED, 0x88, 0x32, 0x4E,
    0x0E, 0x65, 0xCF, 0x1D, 0xF4, 0xC9, 0xF9, 0x30, 0x56, 0x54, 0xF6, 0x33, 0xF0, 0x85, 0x57, 0x94,
    0x3C, 0xB0, 0x53, 0xCA};

  DSSignature signature;
  signature._key._content.Realloc(sizeof(keyContent));
  memcpy(signature._key._content.Data(), keyContent, sizeof(keyContent));

  int sizeSignature;
  void *ptrSignature = FindAndLoadResource(sizeSignature, &ctx, "version.cfg.sign");
  if (!ptrSignature)
  {
    ErrorMessage(hInstance, "Patch file is corrupted, error 2.");
    return 1;
  }
  signature._content1.Realloc(sizeSignature);
  memcpy(signature._content1.Data(), ptrSignature, sizeSignature);

  int error = VerifySignature(ptr, size, signature);
  if (error != 0)
  {
    ErrorMessage(hInstance, Format("Patch file is corrupted, error %d/%d.", error, GetLastError()));
    return 1;
  }

  // parse "version.cfg"
  QIStrStream in(ptr, size);
  ParamFile versionInfo;
  versionInfo.ParseBin(in);
#endif

  bool isPatch = versionInfo >> "isPatch"; // patching exe to higher version?
  RString versionPatch = versionInfo >> "version";
  RString addonName;
  ConstParamEntryPtr entry = versionInfo.FindEntry("name");
  if (entry) addonName = *entry;
#ifndef _SUPER_RELEASE


  // multipatch determine that this patch should be used to update more distributions (OA and RFT)
  ParamEntryPtr multiPatch = versionInfo.FindEntry("multiPatch");
  if (!multiPatch)
  {
    return PatchingThreadFunctionPatch(hInstance,hPrevInstance,lpCmdLine,nCmdShow,console,versionInfo,ctx,isPatch,versionPatch,addonName);
  }

  if (!multiPatch->IsArray())
  {
    ErrorMessage(hInstance, "Property \"multiPatch\" is not array type");
    return 1;
  }

  int ret = 0;
  bool oncePatched = false; // if last multipatch entry and patch doesn't patch anything report
  for (int i=multiPatch->GetSize()-1; i>=0;--i)
  {
    RString name = (*multiPatch)[i];
    ParamEntryPar versionSubInfo =  versionInfo >> name;
    ret = PatchingThreadFunctionPatch(hInstance,hPrevInstance,lpCmdLine,nCmdShow,console,versionSubInfo,ctx,isPatch,versionPatch,addonName,(!oncePatched && i==0),name); 
    if (ret==0) oncePatched = true;
  }

  // apply commands also in root of multipatch 
  ApplyCommands(versionInfo,hInstance, hPrevInstance, nCmdShow, console); 
  
  return oncePatched ? 0 : ret;
#else
  return PatchingThreadFunctionPatch(hInstance,hPrevInstance,lpCmdLine,nCmdShow,console,versionInfo,ctx,isPatch,versionPatch,addonName);
#endif
}

DWORD THREAD_PROC_MODE PatchingThread( void *param )
{
  PatchingThreadContext &cx = *(PatchingThreadContext*)param;
  int retval = PatchingThreadFunction(cx.hInstance, cx.hPrevInstance, cx.lpCmdLine, cx.nCmdShow, cx.console);
  return (DWORD)retval;
}

/// Check access rights to "Program Files" directory and "HKEY_LOCAL_MACHINE/SOFTWARE" registry
static bool CheckAccessRights()
{
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
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE", 0, KEY_READ, &key) != ERROR_SUCCESS)
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

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
  // check if current user privileges allow the run of the setup
  if (!CheckAccessRights())
  {
    char buf[2048];
    LoadAppString(hInstance, IDS_NO_ACCESS_RIGHTS, buf, sizeof(buf));
    ErrorMessage(hInstance, buf);
    return 4;
  }

  Ref<ConsoleWindowProgress> console;

  HMODULE cc = LoadLibrary("comctl32.dll");
  if (cc)
  {
    typedef WINCOMMCTRLAPI void WINAPI InitCommonControlsT(void);
    InitCommonControlsT *icc = (InitCommonControlsT *)
      GetProcAddress(cc,"InitCommonControls");
    if (icc)
    {
      (*icc)();
      // create progress bar window
      console = new ConsoleWindowProgress;
      if (!console) { FreeLibrary(cc); return 1; }
      console->Create(hInstance, false); //create but do not show yet
      console->EnableOK(false);
      console->PrintF(IDS_INTRO_TEXT);
    }
  }
  //thread
  int mainExitCode = 0;
  PatchingThreadContext threadCx(hInstance, hPrevInstance, lpCmdLine, nCmdShow, console);
  ThreadId patchThreadId;
  if ( poThreadCreate(&patchThreadId,32*1024*1024,&PatchingThread,&threadCx) ) 
  {
    HANDLE threadHandle = (HANDLE)patchThreadId;
    while (MsgWaitForMultipleObjects(1, &threadHandle, FALSE, 1000, QS_ALLINPUT)!=WAIT_OBJECT_0)
    {
      MSG msg;
      int status;
      while ( status = PeekMessage(&msg, 0, 0, 0, PM_REMOVE) )
      {
        if (status == -1) 
        { 
          if (cc) FreeLibrary(cc); 
          return 0; 
        }
        switch(msg.message) 
        {
          case WM_QUIT: 
          { 
            if (cc) FreeLibrary(cc); 
            return 0; 
          }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
    DWORD exitCode = 0;
    if (GetExitCodeThread(patchThreadId, &exitCode)) mainExitCode = exitCode;
    console->InitProgressBar(100);
    console->SetProgressBar(100); //show 100%
    if (!exitCode || console->ShouldWaitForClosing())
    {
      console->ShowWindow(true); //maybe it was not shown yet (some early error, patch corrupted)
      console->WaitForClosing();
    }
  }
  else mainExitCode = 3;

  if (cc) FreeLibrary(cc);
  return mainExitCode;
}

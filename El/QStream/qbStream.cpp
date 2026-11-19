#include <El/elementpch.hpp>
#include "qbStream.hpp"
#ifdef _WIN32
  #include <direct.h>
  #include <io.h>
#endif
#include <Es/Strings/bString.hpp>
#include <Es/Files/fileContainer.hpp>
#include <es/Framework/appFrame.hpp>

#include <Es/Common/win.h>
#include <Es/Algorithms/qsort.hpp>

#if _ENABLE_PBO_PROTECTION
#include <El/Encryption/PBOHeaderProtection.h>
#endif
#if MT_SAFE
#define EXCLUSIVE() ScopeLockSection lock(_lock)
#else
#define EXCLUSIVE()
#endif

QFBank::QFBank()
{
  EXCLUSIVE();
  #if !_RELEASE
    _serialize=false;
  #endif
  _error = true; // no open called yet
  _locked = true;
  _loaded = false;
  _lockable = false;
  _verifySignature = false;
#if _ENABLE_PATCHING
  _patched = false;
#endif
#if _ENABLE_PBO_PROTECTION
  _headerStart = 0;
#endif
}

#define WIN_DIR '\\'
#define UNIX_DIR '/'

#if !_RELEASE
#define BEG_SERIALIZE {Assert(!_serialize);_serialize=true;}
#define END_SERIALIZE {Assert(_serialize);_serialize=false;}
#else
#define BEG_SERIALIZE {}
#define END_SERIALIZE {}
#endif

#if defined _M_PPC 
// PowerPC is big endian
static int LoadInt(HANDLE f )
{
  Fail("Big endian from handle not implemented");
  int i = 0;
  DWORD rd = 0;
  ReadFile(f,&i,sizeof(i),&rd,NULL);
  if (rd!=sizeof(i)) i=0;
  return i;
}
#else
static int LoadInt(HANDLE f )
{
  int i = 0;
  DWORD rd = 0;
#if POSIX_FILES_COMMON
  ReadFilePosix(f,&i,sizeof(i),&rd,NULL);
#else
  ReadFile(f,&i,sizeof(i),&rd,NULL);
#endif
  if (rd!=sizeof(i)) i=0;
  return i;
}
#endif
static inline int LoadInt(QIStream &f)
{
  return f.getil();
}

// see also FileBank.cpp

static bool LoadFileInfo( FileInfoO &i, QIStream &f)
{
  // read zero terminated name
  char name[1024];
  char *n=name;
  int maxLen=sizeof(name)-1;
  for (int l=0; l<maxLen; l++)
  {
    int c = f.get();
    if (c<0)
    {
      // error during file reading
      i.name = "";
      i.startOffset = 0;
      i.length = 0;
      return false;
    }
    if (!c) break;
    *n++=c;
  }
  *n=0; // zero terminate in any case
  strlwr(name);
  i.name=name;
  
  int compressedMagic = i.compressedMagic = LoadInt(f);
  i.uncompressedSize = LoadInt(f);
  i.startOffset = LoadInt(f);
  int time = i.time = LoadInt(f);
  i.length = LoadInt(f);

  return compressedMagic==VersionMagic && i.length==0 && time==0;
}

bool GLogFileOps=false;

// simple logging - log what is used, do not check for double files

class QFBankLog: public IBankLog
{
  FILE *_file;
  FindArray<RString> _files; // files aready there

  public:
  void Init(const char *bankName);
  void LogFileOp(const char *name);
  void Flush(const char *bankName);

  QFBankLog();
  ~QFBankLog();

  private:
  void Close();
};

//! create path to given directory / file
static void CreatePath(const char *path)
{
  // string will be changed temporary
  char *end = unconst_cast(path);
  if (end[0] != 0 && end[1] == ':' && end[2] == '\\') end += 3;
  while (end = strchr(end, '\\'))
  {
    *end = 0;
#ifdef _WIN32
    ::CreateDirectoryA(path, NULL);
#else
    ::CreateDirectory(path, NULL);
#endif
    *end = '\\';
    end++;
  }
}

/*!
\patch_internal 5095 Date 12/5/2006 by Jirka
- Fixed: option -logfiles fixed to support long prefixes
*/

void QFBankLog::Init(const char *bankName)
{
  Close();

  BString<1024> logName;
  strcpy(logName,"FilesUsed");
  strcat(logName,"\\");
  strcat(logName,bankName);
  if ( logName.BString<1024>::operator[](strlen(logName)-1)=='\\') logName.BString<1024>::operator[](strlen(logName)-1) = 0;
  strcat(logName,".log");

  ::CreatePath(logName);

#ifndef _WIN32
  logName.UnixPath();
#endif
  FILE *file = fopen(logName,"r");
  if (file)
  {
    // read filenames already there
    for(;;)
    {
      char buf[1024];
      *buf = 0;
      fgets(buf,sizeof(buf),file);
      if (!*buf) break;
      if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;
      _files.Add(buf);
    }
    fclose(file),file = NULL;
  }
  _file = fopen(logName,"a+");
}

void QFBankLog::LogFileOp(const char *name)
{
  if (!_file) return;
  // check if file is already there
  if (_files.Find(name)>=0) return;
  _files.Add(name);
  fprintf(_file,"%s\n",name);

  fflush(_file);
}

void QFBankLog::Flush(const char *bankName)
{
  if (_file) fflush(_file);
}

QFBankLog::QFBankLog()
{
  _file = NULL;
}

void QFBankLog::Close()
{
  if (_file)
  {
    fclose(_file);
    _file = NULL;
  }
}

QFBankLog::~QFBankLog()
{
  Close();
}

RString LoadFromFile(QIStream &in)
{
  BString<1024> buf;
  for(;;)
  {
    char c[2];
    int c0 = in.get();
    if (c0==EOF) break;
    if (c0==0) break;
    c[0] = c0;
    c[1] = 0;
    buf += c;
  }
  return RString(buf);
}

bool StrIsLower(const char *str)
{
  if ( !str ) return true;
  do {
      if ( isupper(*str) ) return false;
  } while ( *(++str) );
  return true;
}

//bool SaveToFile(HANDLE file, RString value)
//{
//  int len = value.GetLength();
//  DWORD size = sizeof(len);
//  BOOL ok = WriteFile(file,&len,size,&size,NULL);
//  return ok!=FALSE;
//}
/*!
\param name filename of the bank
\param callback function called after header is loaded,
used to determine if bank should be loaded
\param context context passed to callback
\return true when bank was succesfully opened
Note: bank opening may be deferred.
Bank may only remmeber all necessary parameter when this function is called
and actual "opening" may be performed later. This should be almost transparent to user.
*/

bool QFBank::open
(
  RString name, OpenCallback beforeOpen, BankContextBase *context
)
{
  BString<1024> fullName;

  strcpy(fullName,name);
  strcat(fullName,".pbo");

  // check if the file exists
  if (!QIFileFunctions::FileExists(fullName))
#if _VBS2 //also accept .ebo files (encrypted pbo)
  {
    bool fileExists = false;
    strcpy(fullName,name);
    strcat(fullName,".ebo");
    if (QIFileFunctions::FileExists(fullName))
      fileExists = true;
    else
    {
      strcpy(fullName,name);
      strcat(fullName,".xbo");   
      if(QIFileFunctions::FileExists(fullName))
        fileExists = true;
    }
    if(!fileExists)
#endif
    {
      ErrorMessage("Cannot open file '%s'",(const char *)fullName);
#ifndef _WIN32
      if (!StrIsLower(cc_cast(fullName))) 
        ErrorMessage("The filename '%s' is not lowercase. Run ./install to convert it!", cc_cast(fullName));
#endif
      return false;
    }
#if _VBS2
  }
#endif

  _openName = fullName.cstr();

  _openBeforeOpenCallback = beforeOpen;
  _openContext = context;
  _files.Clear();
#if _ENABLE_PERFLOG
  _filesByOffset.Clear();
#endif
  _error = false; // no open called yet
  //_openCount = 0;
  return Init();
}

bool QFBank::openFromFile
(
 RString name, OpenCallback beforeOpen, BankContextBase *context, QFileSize start, QFileSize size
)
{
  // check if the file exists
  if (!QIFileFunctions::FileExists(name))
  {
    ErrorMessage("Cannot open file '%s'",cc_cast(name));
#ifndef _WIN32
    if (!StrIsLower(cc_cast(name))) 
      ErrorMessage("The filename '%s' is not lowercase. Run ./install to convert it!", cc_cast(name));
#endif
    return false;
  }

  _openName = name;

  _openBeforeOpenCallback = beforeOpen;
  _openContext = context;
  _files.Clear();
#if _ENABLE_PERFLOG
  _filesByOffset.Clear();
#endif
  _error = false; // no open called yet
  //_openCount = 0;
  if (size)
  {
    LogF("Load bank %s",cc_cast(name));
    Ref<IFileBuffer> wholeBuf = new FileBufferLoading(name);
    _fileAccess = new FileBufferSub(wholeBuf, start, size);
    if (_fileAccess->GetError())
    {
      ErrorMessage("Error opening bank %s",cc_cast(name));
      _error = true;
      return false;
    }
  }
  return Init();
}

bool QFBank::open
(
 IFileBuffer *buffer,
 OpenCallback beforeOpen, BankContextBase *context
)
{
  _openBeforeOpenCallback = beforeOpen;
  _openContext = context;
  _fileAccess = buffer;
  _files.Clear();
#if _ENABLE_PERFLOG
  _filesByOffset.Clear();
#endif
  _error = false; // no open called yet
  return Init();
}

bool QFBank::InitFileAccess()
{
  if (_fileAccess) return true;
  LogF("Load bank %s",(const char *)_openName);

  RString openNameF = _openName;

    // map whole file
  _fileAccess = new FileBufferLoading(openNameF);
    if (_fileAccess->GetError())
    {
      // TODO: localize
      ErrorMessage("Error opening bank %s",(const char *)openNameF);
      _error = true;
      return false;
    }
  return true;
}

bool QFBank::Init()
{
#if _ENABLE_PBO_PROTECTION
  _headerStart = 0;
#endif
  if (!InitFileAccess())
  {
    return false;
  }

  // open a stream to get headers
  QIStreamDataSource in;
  in.AttachSource(_fileAccess);

  // first count how many file infos we have, and count start offset
  int startPos = in.tellg();
  FileInfoO info;
  bool version = LoadFileInfo(info,in);
  if (info.name.GetLength()==0)
  {
    // if bank is empty, it may be "new bank" with product identification
    // check if there is normal terminator, or special
    if (version)
    {
      // read properties
      for(;;)
      {
        RString name = LoadFromFile(in);
        if (name.GetLength()==0)
        {
          break;
        }
        RString value = LoadFromFile(in);
        // some properties are handled differently
        if (!strcmpi(name,"prefix"))
        {
          if (_prefix.GetLength()==0)
          {
            SetPrefix(value+"\\");
          }
        }
        else
        {
          QFProperty &prop = _properties.Append();
          prop.name = name;
          prop.value = value;
        }
      }
    }


  #if _ENABLE_PBO_PROTECTION
    _headerStart = in.tellg();
  #endif
    in.seekg(startPos);
  }
  
  // call callback function
  if(_openBeforeOpenCallback)
  {
    bool ok = _openBeforeOpenCallback(this,_openContext);
    if (!ok)
    {
      _error = true;
      return false;
    }
  }


  return true;
}

/*!
All banks are locked by default. If application bank can be unloaded, it must unlock it.
*/

void QFBank::Lock() const
{
  _locked = true;
  Load();
}

void QFBank::Unlock() const
{
  _locked = false;
  if (_fileAccess && _fileAccess->RefCounter()==1)
  {
    Unload();
  }
}

bool QFBank::CanBeUnloaded() const
{
  return _fileAccess && _fileAccess->RefCounter()==1 && _loaded;
}

static inline RStringB ConvertDirSlash(RStringB name)
{
  const char *change=strchr(name,UNIX_DIR);
  if (!change) return name;
  // make sure name is mutable
  RString mutableName = name;
  char *mutName = mutableName.MutableData();
  for(;;)
  {
    char *change=strchr(mutName,UNIX_DIR);
    if (!change) break;
    *change=WIN_DIR;
  }
  return mutableName;
}

#if _ENABLE_PBO_PROTECTION
void QFBank::GetBankHeaderProtectionInfo(BankHeaderProtectionInfo& info) const
{
  info.isProtected = false;

  Assert(_headerStart > 0);

  // check protection property
  RString prop = GetProperty(PBOHeaderProtection::PBOPropertyName);
  if (prop.IsEmpty())
    return;

  // open a stream
  QIStreamDataSource in;
  in.AttachSource(_fileAccess);

  // remember pos
  QFileSize oldPos = in.tellg();

  in.seekg(_headerStart);
  PBOHeaderProtection::DecryptedHeaderInfo decInfo;
  if (PBOHeaderProtection::GetProtectionInfo(in, decInfo))  
  {
    if (decInfo.isEncrypted)
    {
      info.isProtected = decInfo.isEncrypted;
      info.protHeaderSize = decInfo.headerSizeWithPadding;
      info.headerSize = decInfo.headerSize;
      info.protHeaderStart = _headerStart;
    }
  }

  // seek to old pos
  in.seekg(oldPos);
}

bool QFBank::DecryptHeaders(QOStream &out, QIStream &in, int rangeBeg, int rangeEnd) const
{
  in.seekg(rangeBeg);
// Setup and Serial header protections, see https://wiki.bistudio.com/index.php/Addon_Data_Protection
#if _USE_SETUP_PBO_PROTECTION
  SRef<IFilebankEncryption> headerProtection(PBOHeaderProtection::CreateSetupHeaderProtection());
#else
  SRef<IFilebankEncryption> headerProtection(CreateSerialHeaderProtection());
#endif
  if (headerProtection)
    return PBOHeaderProtection::DecryptHeaders(out, in, headerProtection);
  else
    return false;
}

/// \param pboRegProp value of property of PBo that determines path to registry for serial number
bool GetHeaderProtectionSerials(const RString& pboRegProp, PBOHeaderProtection::TByteBuffer& outSerial1, PBOHeaderProtection::TByteBuffer& outSerial2)
{
  //if (PBOHeaderProtection::CreateTestSerialsFromTestStrings(outSerial1, outSerial2))
    //return true;

  //PBOHeaderProtection::CreateTestSerial(outSerial1, true);
  //PBOHeaderProtection::CreateTestSerial(outSerial2, false);

  // get main app registry serial key
  RString parentAppRegistryPath = PBOHeaderProtection::AppRegistryPath;
  return PBOHeaderProtection::CreateProtectionSerials(parentAppRegistryPath, pboRegProp, outSerial1, outSerial2);
}

IFilebankEncryption* QFBank::CreateSerialHeaderProtection() const
{
  // get PBO property for serial number registry path
  RString regSerialProt = GetProperty(PBOHeaderProtection::PRORegSerialProp);
  if (regSerialProt.IsEmpty())
  {
    Assert(false);
    RptF("Failed to get serial numbers for header decryption.");
    return false;
  }

  PBOHeaderProtection::TByteBuffer serial1;
  PBOHeaderProtection::TByteBuffer serial2;
  if (!GetHeaderProtectionSerials(regSerialProt, serial1, serial2))
  {
    Assert(false);
    RptF("Failed to get serial numbers for header decryption.");
    return false;
  }

  /// create protection that we will use to encrypt headers
  return PBOHeaderProtection::CreateSerialHeaderProtection(serial1, serial2);
}
#endif//#if _ENABLE_PBO_PROTECTION

bool QFBank::Load()
{
  if (!_locked)
  {
    RptF("Cannot open bank %s that is not locked",(const char *)_prefix);
    return false;
  }
  if (_error) return false;
  // TODO: implement some timing. GlobalTickCount is not available here.
  //_lastOpen = GlobalTickCount();
  if (_loaded) return true;
  _files.Clear();
#if _ENABLE_PERFLOG
  _filesByOffset.Clear();
#endif
  EXCLUSIVE();
  // note: name should not contain extension
  // automatic optimal bank type is performed with different extensions
  // like .pbf and .pbo

  if (!InitFileAccess())
  {
    return false;
  }
  // open a stream to get headers
  QIStreamDataSource in;
  in.AttachSource(_fileAccess);

  int fileCount = 0;
  FileInfoO info;

  // first count how many file infos we have, and count start offset
  int startPos = in.tellg();
  for(;;)
  {
    LoadFileInfo(info,in);
    if( info.name.GetLength()<=0 ) break;
    fileCount++;
  }
#if !_VBS2_LITE
  _files.Reserve(fileCount);
#endif

  int startOffset=0;
  in.seekg(startPos);
  bool lastIsVersion = false;
  for(;;)
  {
    // from optimized bank different loading
    lastIsVersion = LoadFileInfo(info,in);
    
    info.startOffset = startOffset;
    startOffset += info.length;
    if( info.name.GetLength()<=0 ) break;

    info.name = ConvertDirSlash(info.name);
    
#if !_VBS2_LITE
    _files.Add(info);
#endif
  }
  // if bank is empty, it may be "new bank" with product identification
  // check if there is normal terminator, or special
  if (_files.NItems()==0 && lastIsVersion)
  {
    // read properties
    for(;;)
    {
      RString name = LoadFromFile(in);
      if (name.GetLength()==0)
      {
        break;
      }
      RString value = LoadFromFile(in);
      // properties are already read during open/Init
      // we can skip them now
    }


#if _ENABLE_PBO_PROTECTION
    // check if header is protected using encryption
    bool headerProtection = false;
    RString headerProtectionProp = GetProperty(PBOHeaderProtection::PBOPropertyName);
    if (!headerProtectionProp.IsEmpty())
    {
      // remember current pos
      QFileSize tmpPos = in.tellg();
      // read int
      int version = LoadInt(in);
      if (PBOHeaderProtection::GetProtectionVersion() == version)
      {
        // version matches, header is really protected
        headerProtection = true;
      }
      else
      {
#if _SUPER_RELEASE
        // in retail disable reading PBO's that are prepared for protection, but not protected
        ErrF("Failed to create serial header protection class.");
        ErrorMessage(EMError,"Failed to load file \"%s\" - file should be protected.",cc_cast(_openName));
        _error = true;
        return false;
#endif
      }
      // return to position before version
      in.seekg(tmpPos);
    }
#endif //#if _ENABLE_PBO_PROTECTION

    // read normal file headers
    RString encryption = GetProperty("encryption");
    if (encryption.GetLength()>0)
    {
      // we need to load encrypted headers
      // for this we need to know headers encrypted size
      // load decoded size
      int headersSize = LoadInt(in);
      int headersEncodedSize = LoadInt(in);
      // read encoded headers into memory
      Temp<char> headers(headersEncodedSize);
      int rd = in.read(headers.Data(),headers.Size());
      if (rd==headers.Size())
      {
        Ref<IFilebankEncryption> ss = CreateFilebankEncryption(encryption,NULL);
        if (ss)
        {
          QIStrStream headersEncoded(headers.Data(),headers.Size());
          Temp<char> headerDecodedData(headersSize);
#if _VBS2 
          if( strcmpi((const char*) encryption,"vbs2")==0 ||
              strcmpi((const char*) encryption,"vbs2m")==0 )
          {
            // read in 16 bytes from the header, this should contain the files initvec
            unsigned char inVec[16] = { 
              0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
              0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c 
            };
            
            int startPos = in.tellg();
            int invcHeader = in.read(inVec,16);
            in.seekg(startPos); // move back to previous position

            if(invcHeader != 16)
            {
              LogF("Failed to get invector on bank:%s", (const char*)_openName );
              _files.Clear();
              _error = true;
              return false;
            }
            else
              ss->Decode(headerDecodedData.Data(),headerDecodedData.Size(),headersEncoded,inVec);
          }
          else
          ss->Decode(headerDecodedData.Data(),headerDecodedData.Size(),headersEncoded);
#else
          ss->Decode(headerDecodedData.Data(),headerDecodedData.Size(),headersEncoded);
#endif
          QIStrStream headersDecoded(headerDecodedData.Data(),headerDecodedData.Size());
          for(;;)
          {
            // from optimized bank different loading
            LoadFileInfo(info,headersDecoded);
            
            info.startOffset = startOffset;
            startOffset+=info.length;
            if( info.name.GetLength()<=0 ) break;

            info.name = ConvertDirSlash(info.name);
            _files.Add(info);
          }
        }
      }
    }
#if _ENABLE_PBO_PROTECTION
    else if (headerProtection)
    {
      // headers are protected using encryption

      /// create protection that we will use to encrypt headers
// Setup and Serial header protections, see https://wiki.bistudio.com/index.php/Addon_Data_Protection
#if _USE_SETUP_PBO_PROTECTION
      SRef<IFilebankEncryption> headerProtection(PBOHeaderProtection::CreateSetupHeaderProtection());
#else
      SRef<IFilebankEncryption> headerProtection(CreateSerialHeaderProtection());
#endif
      if (headerProtection.IsNull())
      {
        Assert(false);
        ErrF("Failed to create header protection class.");
        ErrorMessage(EMError,"Failed to load file \"%s\" - decryption of headers failed.",cc_cast(_openName));
        _error = true;
        return false;
      }
    
      // remember current pos
      QFileSize tmpPos = in.tellg();

      // decrypt header
      QOStrStream decryptedHeader;
      PBOHeaderProtection::DecryptedHeaderInfo decryptInfo;
      if (!PBOHeaderProtection::DecryptHeaders(decryptedHeader, in, headerProtection, &decryptInfo))
      {
        Assert(false);
        ErrF("Headers of bank \"%s\" cannot be decrypted - decryption failed.", cc_cast(_openName));   
        ErrorMessage(EMError,"Failed to load file \"%s\" - decryption of headers failed.",cc_cast(_openName));
        _error = true;
        return false;
      }

      // put data to input stream
      QIStrStream decHeader(decryptedHeader.str(), decryptedHeader.pcount());
      
      bool firstInfo = true;
      while (true)
      {
        // from optimized bank different loading
        LoadFileInfo(info,decHeader);

        if (firstInfo)
        {
          // first info must be dummy file, otherwise decrypted header is corrupted
          firstInfo = false;

          if (info.name.IsEmpty() || stricmp(info.name.Data(), PBOHeaderProtection::DummyPaddingFileName) != 0)
          {
            ErrF("Decrypted headers of bank \"%s\" are corrupted, bank not loaded.", cc_cast(_openName));   
            ErrorMessage(EMError,"Failed to load file \"%s\" - decryption of headers failed.",cc_cast(_openName));
            _error = true;
            return false;           
          }
        }

        info.startOffset = startOffset;
        startOffset+=info.length;

        if( info.name.GetLength()<=0 ) break;

        info.name = ConvertDirSlash(info.name);
        _files.Add(info);
      }

      // seek to the correct position (at the end of the header)
      in.seekg(tmpPos + decryptInfo.headerSize);
      
    }
#endif //#if _ENABLE_PBO_PROTECTION
    else
    {
#if _VBS2_LITE
      ErrorMessage("Cannot load file:%s",(const char *)_prefix);
      _files.Clear();
      _error = true;
      return false;
#else
      int fileCount = 0;
      int startPos = in.tellg();
      for(;;)
      {
        // from optimized bank different loading
        LoadFileInfo(info,in);
        
        if( info.name.GetLength()<=0 ) break;
        fileCount++;
      }
      _files.Reserve(fileCount);
      in.seekg(startPos);
      for(;;)
      {
        // from optimized bank different loading
        LoadFileInfo(info,in);
        
        info.startOffset = startOffset;
        startOffset+=info.length;
        if( info.name.GetLength()<=0 ) break;

        info.name = ConvertDirSlash(info.name);
        _files.Add(info);
      }
#endif
    }
  }
  else
  {
    // call callback function
    if(_openBeforeOpenCallback)
    {
      bool ok = _openBeforeOpenCallback(this,_openContext);
      if (!ok)
      {
        _files.Clear();
        _error = true;
        return false;
      }
    }
  }


  int headerSize = in.tellg();
  BEG_SERIALIZE
  // filemapping uses offset from end of header
  // direct access uses file offset
  if (_files.NItems() > 0)
  {
    // !!! avoid GetTable when NItems == 0
    for (int i=0; i<_files.NTables(); i++)
    {
      AutoArray<FileInfoO> &container = _files.GetTable(i);
      for (int j=0; j<container.Size(); j++)
      {
        FileInfoO &info = container[j];
        info.startOffset += headerSize;
      }
      //container.Compact();
    }
  }
  startOffset+=headerSize;
  // check integrity - try to seek end of file
  in.seekg(startOffset);
  int checkEof = in.tellg();
  if( checkEof!=startOffset )
  {
    ErrorMessage("Data file too short '%s'. Expected %d B, got %d B",(const char *)_openName,startOffset,checkEof);
  }

  #if _ENABLE_PATCHING
  if (GEnablePatching)
  {
    ScanPatchFiles(_prefix,RString());
  }
  #endif

  _files.Compact();

#if _ENABLE_PERFLOG
  _filesByOffset.Realloc(_files.NItems());
  struct AddFileByOffset
  {
    BinFindArrayKey<FileInfoO *> &_filesByOffset;
    
    AddFileByOffset(BinFindArrayKey<FileInfoO *> &_filesByOffset)
    :_filesByOffset(_filesByOffset){}
    
    bool operator ()(FileInfoO &info, const FileBankType *container)const
    {
      if (info.length>0)
        _filesByOffset.AddUnique(&info);
      return false;
    }
  };
  #if !_DEBUG
  // caution: we use pointers into the hash table
  // the hash table must not be resized while we exist
  _files.ForEachF(AddFileByOffset(_filesByOffset));
  // TODO: consider using QSort instead of a binary search for each item
  //QSort(_filesByOffset,.QSort()
  #endif
#endif

  _loaded = true;
  END_SERIALIZE
  return true;
}

#include <El/Encryption/pboheaderprotection.h>

#if _ENABLE_PBO_PROTECTION
QFileSize QFBank::EncryptHeadersSetup(QOStrStream &out, QOStrStream &in) const
{
  SRef<IFilebankEncryption> headerProtection(PBOHeaderProtection::CreateSetupHeaderProtection());
  return PBOHeaderProtection::EncryptHeaders(out, in, headerProtection);
}
#endif

void QFBank::MakeHandlePermanent()
{
  if (_fileAccess)
  {
    FileServerHandle handle = _fileAccess->GetHandle();
    GFileServerFunctions->MakeReadHandlePermanent(handle);
  }
  else
  {
    LogF("Permanent ignored for %s, not loaded",cc_cast(_openName));
  }
}

void QFBank::Unload()
{
  // if error, there is nothing to undo
  if (_error) return;
  // if there is no handle, there is nothing to undo
  if (!_loaded) return;
  // unload this bank
  if (_fileAccess && _fileAccess->RefCounter()>1)
  {
    // if some file from bank is still used, we cannot unload it
    LogF
    (
      "Cannot unload bank %s, %d files are still open",
      (const char *)_openName,_fileAccess->RefCounter()-1
    );
    return;
  }
  LogF("Unload bank %s",(const char *)_openName);
  _fileAccess.Free();
  _loaded = false;
  Clear();
}

bool QFBank::error() const
{
  // if bank was not opened yet, it cannot have any fatal errors
  if (!_error) return false;
  // if there is no handle, there is some fatal error
  if (!_loaded) return true;
  return false;
}

const RString &QFBank::GetProperty(const RString &name) const
{
  for (int i=0; i<_properties.Size(); i++)
  {
    if (!strcmpi(_properties[i].name,name))
    {
      return _properties[i].value;
    }
  }
  const static RString empty;
  return empty;
}

void QFBank::ScanPatchFiles(RString prefix, RString subdir)
{
#if _ENABLE_PATCHING
  // check if there is folder containing patch files
  BString<1024> wildname;
  strcpy(wildname,prefix);
  //TerminateBy(wildname,"\\");
  strcat(wildname,subdir);

  if (prefix.GetLength()<=0)
  {
    // patching bank with no prefix is nonsense
    // most likely cause is temporary bank (like in single missions)
    return;
  } 
#ifdef _WIN32

  strcat(wildname,"*.*");
  _finddata_t find;
  long hf = X_findfirst(wildname,&find);  // not used on Xbox
  if (hf!=-1)
  {
    do
    {
      BString<1024> lowName;
      strcpy(lowName,find.name);
      strlwr(lowName);
      RString name = lowName;
      //LogF("Checking file %s",find.name);
      if (find.attrib&_A_SUBDIR)
      {
        if (!strcmp(find.name,".") || !strcmp(find.name,"..")) continue;
        ScanPatchFiles(prefix,subdir + name + RString("\\"));
      }
      else
      {
        RString subname = subdir+name;
        const FileInfoO &file = _files[subname];
        if (_files.NotNull(file))
        {
          LogF("Plain file version of %s used",(const char *)(subname));
          FileInfoO fileSet = file;
          fileSet.loadFromFile = true;
          _files.Add(fileSet);
          _patched = true;
        }
      }


    } while (_findnext(hf,&find)==0);
    
    _findclose(hf);
  }

#else

    wildname.UnixPath();
    int len = strlen(wildname);
    if ( len > 0 && wildname[len-1] == UNIX_DIR )
        wildname[--len] = (char)0;
    DIR *dir = OpenDir(wildname);
    if ( !dir ) return;
    struct dirent *entry;
    while ( (entry = readdir(dir)) ) {  // process one directory entry
        if ( entry->d_name[0] == '.' &&
             (!entry->d_name[1] ||
              entry->d_name[1] == '.' &&
              !entry->d_name[2]) )
            continue;
        // stat the entry <= subdirectories must be handled differently
        wildname += "/";
        wildname += entry->d_name;
        struct stat st;
        if ( !stat(wildname,&st) ) {
            if ( S_ISDIR(st.st_mode) ) {// directory
                ScanPatchFiles(prefix,subdir + entry->d_name);
                }
            else {                      // regular file
                RString subname = subdir + entry->d_name;
                const FileInfoO &file = _files[subname];
                if (_files.NotNull(file)) {
                    LogF("Plain file version of %s used",(const char *)(subname));
                    FileInfoO fileSet = file;
                    fileSet.loadFromFile = true;
                    _files.Add(fileSet);
                    _patched = true;
                    }
                }
            }
        wildname[len] = (char)0;
        }
    closedir(dir);

#endif

#endif
}

void QFBank::SetPrefix(RString prefix)
{
  _prefix = prefix;
  // create log
  if (GLogFileOps)
  {
    _log = new QFBankLog();
    _log->Init(prefix);
  }

}

const FileInfoO &QFBank::FindFileInfo( const char *name ) const // check if file exists
{
  EXCLUSIVE();
  BString<1024> lowName;
  strcpy(lowName,name);
  strlwr(lowName);
  return _files[lowName];
}

QFileTime QFBank::TimeStamp() const
{
  if (!_fileAccess) return 0;
  return GFileServerFunctions->TimeStamp(_fileAccess->GetHandle());
}

bool QFBank::FileExists( const char *name ) const // check if file exists
{
  if (!Load()) return false;
  const FileInfoO &info = FindFileInfo(name);
  return NotNull(info);
}

bool QFBank::Contains(const char *path) const
{
  int n = GetPrefix().GetLength();
  if (strnicmp(path, GetPrefix(), n) != 0) return false;
  return FileExists(path + n);
}

void QFBank::Read( QFileSize offset, void *buf, long size, const char *name ) const
{
  Assert(!_error);
  BEG_SERIALIZE
  EXCLUSIVE();  // seek to wanted position
  Log("Do not use - slow");
  QIFStream in;
  in.open(_openName);
  in.seekg(offset);
  int rd = in.read(buf,size);
  if (rd!=size)
  {
    ErrorMessage("Data file read error (%s, %s).",name,(const char *)_openName);
  }
  END_SERIALIZE
}

#include "fileCompress.hpp"

/*!
Function result can be used to determine relative order of two files in bank.
When two files return same result, their order is unknown.
When one file returns higher smaller than another one,
it is nearer the beginning of the bank.

Note: Function results need not be continuous for any bank. When i is returned
for some file, there is no guranntee i+1 or i-1 will be returned for any other file.

The purpose of this function is to give anyone loading many files opportunity
to optimize access order to achieve as sequential access as possible.
*/

int QFBank::GetFileOrder(const char *file)
{
  const FileInfoO &info = FindFileInfo(file);
  if (IsNull(info)) return 0;
  return info.startOffset;
}

//! anything can be opened based on path
class QFBankPointerToAny: public QFBankPointer
{
  RString _path;
  QFileSize _offset;
  QFileSize _size;

  public:
  QFBankPointerToAny(const RString &path, QFileSize offset, QFileSize size);
  Ref<IFileBuffer> OpenSource(QIFStreamB &f, QFileSize offset, QFileSize size);
  Ref<IFileBuffer> OpenSource();
  RString GetDebugName() const {return _path;}
  Ref<QFBankPointer> Partial(QFileSize offset, QFileSize size) const;
};

QFBankPointerToAny::QFBankPointerToAny(const RString &path, QFileSize offset, QFileSize size)
:_path(path)
{
  _offset = offset;
  _size = size;
}

Ref<QFBankPointer> QFBankPointerToAny::Partial(QFileSize offset, QFileSize size) const
{
  if (offset>_size) offset = _size;
  QFileSize maxSize = _size-offset;
  if (size>maxSize) size = maxSize;
  return new QFBankPointerToAny(_path,_offset+offset,size);
}

Ref<IFileBuffer> QFBankPointerToAny::OpenSource()
{
  return QIFStreamB::OpenSource(_path,_offset,_size);
}

QFBankHandle QFBank::GetHandle(const char *fullName, const char *filename, QFileSize offset, QFileSize size)
{
  // handle must include
  // bank identification 
  // file identification
  return new QFBankPointerToAny(fullName,offset,size);
}

/*!
  \patch 1.05 Date 07/17/2001 by Ondra
  - Improved: Better diagnostics of corrupted datafiles.
*/

Ref<IFileBuffer> QFBank::Read( const char *name, QFileSize offset, QFileSize size ) const
{
  if (!Load()) return NULL;
  // log information about file being opened
  // even when operation is not sucessful, file is logged
  if (_log)
  {
    // log name
    _log->LogFileOp(name);
  }

  const FileInfoO &info = FindFileInfo(name);
  if (IsNull(info))
  {
    return NULL;
  }
  #if _ENABLE_PATCHING
  if (info.loadFromFile)
  {
    // patch file provided - use it
    BString<1024> fullName;
    strcpy(fullName,GetPrefix());
    strcat(fullName,name);
    Ref<IFileBuffer> whole = new FileBufferLoading(fullName);
      if (!whole->GetError())
      {
      return IFileBuffer::SubBuffer(whole,offset,size);
        }
        }
  #endif

  if( info.compressedMagic==CompMagic ) // some compression
  {
    QIStrStream inBuf;
    // read compressed data into temporary buffer
    Ref<QIStreamBuffer> cData = new QIStreamBuffer(info.length);

    //Temp<char> cData(info.length);
    Read(info.startOffset,cData->DataLock(0,info.length),info.length,name);
    cData->DataUnlock(0,info.length);
    inBuf.init(cData);
    // uncompress
    Ref<FileBufferMemory> data=new FileBufferMemory(info.uncompressedSize);
    SSCompress ss;
    if (!ss.Decode(data->GetWritableData(),info.uncompressedSize,inBuf))
    {
      RptF("Error decoding %s from %s",name,(const char *)_prefix);
      return NULL;
    }
    if (offset>0 || size<info.length)
    {
      return new FileBufferSub(data,offset,size);
    }
    return data.GetRef();
  }
  else if (info.compressedMagic==EncrMagic)
  {
#if _VBS2
      RString encypt = GetProperty("encryption");
      if( strcmpi((const char*)encypt,"VBS2")==0 ||
          strcmpi((const char*)encypt,"VBS2M")==0 )
      {
        if (size>info.length-offset) 
          size = info.length-offset;
        
        // check to see if offset is moduls of 16.
        QFileSize nOffset = offset;
        QFileSize nEndPos = nOffset + size;
        
        // grab the inital inVec from the file header information
        unsigned char inVec[16] = { 
          0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
          0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c 
        };

        // Load the inilisation vector
        int *ptr = (int*)inVec;
        for( int x = 0; x < 16; x += sizeof(int),++ptr)
          *ptr = info.uncompressedSize; // keep appending length to InVec

        // move back to the 16 byte boundary
        int remainder = nOffset % 16;
        nOffset -= remainder;

        // Read in the inVector, previous data chunk
        if(nOffset>0)
          Read(info.startOffset+(nOffset-16),inVec,16,name);

        // check that end poisition is a modules of 16
        remainder = nEndPos % 16;
        if(remainder>0)
          nEndPos += 16 - remainder; // move forward

        QFileSize newSize = nEndPos-nOffset;
        QIStrStream inBuf;
        Ref<QIStreamBuffer> cData = new QIStreamBuffer(newSize);

        Read(info.startOffset+nOffset,cData->DataLock(0,newSize),newSize,name);
        cData->DataUnlock(0,newSize);
        inBuf.init(cData);

        Ref<FileBufferMemory> data=new FileBufferMemory(newSize); 
        Ref<IFilebankEncryption> ss = CreateFilebankEncryption(encypt,NULL);     
        if (!ss || !ss->Decode(data->GetWritableData(),newSize,inBuf,inVec))
        {
          RptF("Error decoding %s from %s",name,(const char *)_prefix);
          return NULL;
        }
        
        if (offset > 0 || size < info.length)
        {
          QFileSize moveToOffset = offset - nOffset;
          return new FileBufferSub(data,moveToOffset,newSize);
        }
        
        return data.GetRef();
      }
#endif 
    QIStrStream inBuf;
    // read compressed data into temporary buffer
    Ref<QIStreamBuffer> cData = new QIStreamBuffer(info.length);

    Read(info.startOffset,cData->DataLock(0,info.length),info.length,name);
    cData->DataUnlock(0,info.length);

    inBuf.init(cData);
    // ask compression manager to create encryptor/decryptor
    // uncompress
    Ref<FileBufferMemory> data=new FileBufferMemory(info.uncompressedSize);
    Ref<IFilebankEncryption> ss = CreateFilebankEncryption(GetProperty("encryption"),NULL);
    if (!ss || !ss->Decode(data->GetWritableData(),info.uncompressedSize,inBuf))
    {
      RptF("Error decoding %s from %s",name,(const char *)_prefix);
      return NULL;
    }
    if (offset>0 || size<info.length)
    {
      return new FileBufferSub(data,offset,size);
    }
    return data.GetRef();
  }
  else if (info.compressedMagic==0)
  {
    // no compression
    if (size>info.length-offset) size = info.length-offset;


    Ref<IFileBuffer> data = new FileBufferSub
    (
      _fileAccess,info.startOffset+offset,size
    );
    return data;
  }
  else
  {
    // some unknown compression manager
    Fail("Unknown compression manager");
    return NULL;
  }
}

bool QFBank::HandleOwned(const FileServerHandle &handle) const
{
  if (!_fileAccess) return false;
  return handle == _fileAccess->GetHandle();
}

//typedef MapStringToClass<FileInfoO, AutoArray<FileInfoO> > FileBankType;
//ForEach(void (*Func)(const Type &, const MapStringToClass *, void *), void *context=NULL) const;

void QFBank::ForEach
(
  void (*Func)(const FileInfoO &fi, const FileBankType *files, void *context),
  void *context
) const
{
  if (!Load()) return;
  // call Func for all files
  EXCLUSIVE();
  _files.ForEach(Func,context);
}

struct CheckOffsetContext
{
  RString name;
  QFileSize offsetBeg,offsetEnd;
  
};
static void CheckOffset(const FileInfoO &fi, const FileBankType *files, void *context)
{
  CheckOffsetContext *ctx = (CheckOffsetContext *)context;
  // check if offset interval intersection is non empry
  unsigned int fiEnd = fi.startOffset+fi.length;
  if (fi.length==0) return;
  if (ctx->offsetBeg>=fiEnd) return;
  if (ctx->offsetEnd<=fi.startOffset) return;
  if (ctx->name.GetLength()>0)
  {
    ctx->name = ctx->name+","+fi.name;
  }
  else
  {
    ctx->name = cc_cast(fi.name); // cc_cast for thread safety
  }
}

RString QFBank::FileOnOffset(QFileSize offsetBeg,QFileSize offsetEnd) const
{
  #if _ENABLE_PERFLOG
  if (!_loaded) return RString();
  if (_filesByOffset.Size()<=0) return RString();
  #if 0 // _DEBUG
  CheckOffsetContext ctx;
  ctx.offsetBeg = offsetBeg;
  ctx.offsetEnd = offsetEnd;
  ForEach(CheckOffset,&ctx);
  // we will verify if the fast approach gives the same list
  #endif
  RString name;
  bool foundB;
  bool foundE;
  int posB = _filesByOffset.FindKeyPos(offsetBeg,foundB);
  int posE = _filesByOffset.FindKeyPos(offsetEnd,foundE);
  Assert(posB>=0 && posB<=_filesByOffset.Size());
  Assert(posE>=0 && posE<=_filesByOffset.Size());
  for (int pos=intMax(posB-1,0); pos<=intMin(posE,_filesByOffset.Size()-1); pos++)
  {
    FileInfoO *fi = _filesByOffset[pos];
    if (offsetBeg<fi->startOffset+fi->length && offsetEnd>fi->startOffset)
    {
      #if 0 // _DEBUG
        // verify: each item found must be present in the list
        Assert(strstr(ctx.name,fi->name));
      #endif
      if (name.GetLength()>0)
      {
        name = name+","+fi->name;
}
      else
      {
        name = cc_cast(fi->name); // cc_cast for thread safety
      }
    }
  }
  #if 0 // _DEBUG
    // verify: total list size must be the same
    Assert(strlen(ctx.name)==strlen(name));
  #endif
  return name;
  #else
  return RString();
  #endif
}

RString QFBank::FileOnOffset(
  const FileServerHandle &handle, QFileSize offsetBeg, QFileSize offsetEnd
)
{
  BankList::ReadAccess read(GFileBanks);
  for (int i=0; i<read.Size(); i++)
  {
    if (read[i].HandleOwned(handle))
    {
      return read[i].FileOnOffset(offsetBeg,offsetEnd);
    }
  }
  return RString();
  
}

void QFBank::Clear()
{
  EXCLUSIVE();
  // clear variables that are not part of Global structure
  _files.Clear();
  _fileAccess.Free();
#if _ENABLE_PERFLOG
  _filesByOffset.Clear();
#endif
  
}

QFBank::~QFBank()
{
  EXCLUSIVE();
  Clear();
}

Ref<IFileBuffer> QIFStreamB::OpenFromBank( const QFBank &bank, const char *name, QFileSize offset, QFileSize size )
{
  return bank.Read(name,offset,size);
}

void QIFStreamB::open(const QFBank &bank, const char *name)
  {
  Ref<IFileBuffer> source = OpenFromBank(bank,name);
  OpenBuffer(source);
  }

void QIFStreamB::open(const QFBankHandle &handle)
{
  Ref<IFileBuffer> source = OpenFromBank(handle);
  OpenBuffer(source);
}

BankList GFileBanks;
bool GUseFileBanks;

#if _ENABLE_PATCHING
bool GEnablePatching = true;
#endif

bool QFBank::FreeUnusedBanks(size_t sizeNeeded)
{
  BankList::WriteAccess access(GFileBanks);
  return GFileBanks.UnloadUnused();
}

struct FindDataContext
{
  QFileSize offsetBeg;
  QFileSize offsetEnd;
  FindDataContext()
  {
    offsetBeg = INT_MAX;
    offsetEnd = 0;
  }
};

static void FindData(const FileInfoO &fi, const FileBankType *files, void *context)
{
  FindDataContext *ctx = (FindDataContext *)context;

  if (fi.startOffset < ctx->offsetBeg) ctx->offsetBeg = fi.startOffset;
  if (fi.startOffset + fi.length > ctx->offsetEnd) ctx->offsetEnd = fi.startOffset + fi.length;
}

bool QFBank::FindData(int &dataBeg, int &dataEnd) const
{
  // find data start and end
  FindDataContext ctx;
  ForEach(::FindData, &ctx);
  dataBeg = ctx.offsetBeg;
  dataEnd = ctx.offsetEnd;
  return dataEnd > 0;
}

bool QFBank::GetHash(Temp<char> &hash) const
{
  if (!_loaded) return false;

  int dataBeg, dataEnd;
  if (!FindData(dataBeg, dataEnd)) return false;

  QIStreamDataSource in;
  in.AttachSource(_fileAccess);
  int fileOld = in.tellg(); // store original position

  int size = _fileAccess->GetSize() - dataEnd - sizeof(bool);

  if (size > 0)
  {
    hash.Realloc(size);
    in.seekg(dataEnd + sizeof(bool));
    in.read(hash.Data(), size);
  }

  in.seekg(fileOld); // move read pointer to original position

  return size > 0;
}


#ifdef _WIN32

FindBank::FindBank()
{
  _handle = -1;
}

FindBank::~FindBank()
{
  Close();
}

bool FindBank::First(const char *path)
{
  strcpy(_wild,path);
#if _VBS2
  strcat(_wild,"\\*.*bo");
#else
  strcat(_wild,"\\*.pbo");
#endif
  _info = new _finddata_t;
  _handle = _findfirst(_wild, (_finddata_t *)_info);
  return _handle != -1;
}

bool FindBank::Next()
{
  if (!_info || _handle==-1) return false;
  return _findnext(_handle, (_finddata_t *)_info)==0;
}

void FindBank::Close()
{
  if (_info)
  {
    delete (_finddata_t *)_info;
    _info = NULL;
  }
  if (_handle!=-1)
  {
    _findclose(_handle);
    _handle = -1;
  }
}

const char *FindBank::GetName() const
{
  if (!_info) return "";
  return ((_finddata_t *)_info)->name;
}

#else

FindBank::FindBank()
{
  dir = NULL;
  entry = NULL;
}

FindBank::~FindBank()
{
  Close();
}

bool FindBank::First(const char *path)
{
    Close();
    LocalPath(dirName,path);
    dir = OpenDir(dirName);
    return Next();
}

bool FindBank::Next()
{
    if ( !dir ) return false;
    while ( (entry = readdir(dir)) ) {
        int len = strlen(entry->d_name);
        if ( len <= 4 ) continue;
        if ( !strcmp(entry->d_name+len-4,".pbo") )
            return true;
        }
    closedir(dir);
    dir = NULL;
    return false;
}

void FindBank::Close()
{
    if ( dir ) {
        closedir(dir);
        dir = NULL;
        entry = NULL;
        }
}

const char *FindBank::GetName() const
{
    if ( !dir || !entry ) return "";
  return entry->d_name;
}

#endif

/*!
  @return the bank which was loaded
  Note: callback may be called even after Load returns
  Context is stored meanwhile inside the bank.
*/

QFBank *BankList::Load(
  const RString &path,
  const RString &bankPrefix,const RString &bName,
  bool emptyPrefix,
  OpenCallback beforeOpen, OpenCallback afterOpen, BankContextBase *context
)
{
  #if _DEBUG
    if (strstr(bName,"dubbingradio_e"))
    {
      __asm nop;
    }
  #endif
  Ref<QFBank> newBank = new QFBank;
  QFBank &bank = *newBank;
  // path can differ from bankPrefix (for example if Mod is used)
  if (!bank.open(path+bName,beforeOpen,context))
  {
    return NULL;
  }
  Log("Open bank %s",(const char *)(bankPrefix+bName));

  if (bank.error())
  {
    return NULL;
  }

  if (bank.GetPrefix().GetLength()==0)
  {
    // if no prefix is stored in the bank, use automatic prefixing
    RString prefix;
    if (emptyPrefix) prefix = bName;
    else prefix = bankPrefix+bName;

    RString prefixPath = prefix+"\\";
    bank.SetPrefix(prefixPath);
  }

  if (afterOpen && !afterOpen(&bank,context))
  {
    return NULL;
  }

  // TODO: change Add signature instead
  Add(newBank);
  return &bank;
}

void BankList::Unload(const RString & bankPrefix,const RString &bName, bool emptyPrefix)
{
  // find corresponding bank
  RString prefix;
  if (emptyPrefix) prefix = bName;
  else prefix = bankPrefix+bName;

  RString prefixPath = prefix+"\\";

  WriteAccess write(*this);
  for (int i=0; i<write.Size(); i++)
  {
    const QFBank &b = write.Get(i);
    if(b.GetPrefix()==prefixPath)
    {
      _banks.Delete(i);
      return;
    }
  }
}

void BankList::Remove(const char *prefix)
{
  WriteAccess write(*this);
  int prefixLen = strlen(prefix);
  for (int i=0; i<write.Size();)
  {
    QFBank &bank = write.Set(i);
    if (strnicmp(bank.GetPrefix(), prefix, prefixLen) == 0)
    {
      for (int j=0; j<_flushCallback.Size(); j++) _flushCallback[j](bank);
      _banks.Delete(i);
    }
    else
      i++;
  }
}

void BankList::Remove(QFBank &bank)
{
  WriteAccess write(*this);
  for (int i=0; i<_flushCallback.Size(); i++) _flushCallback[i](bank);
  _banks.DeleteKey(&bank);
}


#include <Es/Algorithms/bSearch.hpp>

int BankList::BankIndex(const QFBank &bank) const
{
  return _banks.FindKey(&bank);
}

QFBank* BankList::FindBank(const char* path)
{
  if (_orderDirty)
  {
    WriteAccess access(*this);
    // sort now so that BSearchPos can work
    struct SortBank
    {
      static int compare(const Ref<QFBank> *a, const Ref<QFBank> *b)
      {
        return strcmpi((*a)->GetPrefix(), (*b)->GetPrefix());
      }
    };
    QSort(_banks.Data(),_banks.Size(),SortBank::compare);
    _orderDirty = false;
  }
  ReadAccess access(*this);
  // comparison functor for BSearchPos
  struct CompareBank
  {
    static int compare(const char* path, const QFBank *item)
    {
      return strcmpi(path, item->GetPrefix());
    }
  };

  // find the position of exact match or of the first greater non-matching prefix
  int pos = BSearchPos(_banks.Data(), _banks.Size(), CompareBank::compare, path);

  // test exact match first
  if (pos < access.Size())
  {
    QFBank& bank = access.Get(pos);
    if (!CmpStartStr(path, bank.GetPrefix()))
      return &bank;
  }

  // the previous prefix should be the longest match 
  if (pos > 0)
  {
    QFBank& bank = access.Get(pos-1);
    if (!CmpStartStr(path, bank.GetPrefix()))
      return &bank;
  }

  // no matching bank found
  return NULL;
}

void BankList::Lock(const RString &prefix)
{
  ReadAccess access(*this);
  for (int i=0; i<access.Size(); i++)
  {
    const QFBank &b = access.Get(i);
    if(b.GetPrefix()==prefix)
    {
      b.Lock();
      return;
    }
  }
  LogF("Lock: Bank %s not found",(const char *)prefix);
}
void BankList::Unlock(const RString &prefix)
{
  ReadAccess access(*this);
  for (int i=0; i<access.Size(); i++)
  {
    const QFBank &b = access.Get(i);
    if(b.GetPrefix()==prefix)
    {
      b.Unlock();
      return;
    }
  }
  LogF("Unlock: Bank %s not found",(const char *)prefix);
}

void BankList::SetLockable(const RString &prefix, bool lockable)
{
  ReadAccess access(*this);
  for (int i=0; i<access.Size(); i++)
  {
    QFBank &b = access.Get(i);
    if(b.GetPrefix()==prefix)
    {
      b.SetLockable(lockable);
      return;
    }
  }
  LogF("MakeLockable: Bank %s not found",(const char *)prefix);
}

/*!
\param before unload all banks that have been last used before this time
*/

bool BankList::UnloadUnused()
{
  WriteAccess access(*this);
  // if it is not locked, is is probably already unloaded
  // but we still may want to try it first
  for (int i=0; i<access.Size(); i++)
  {
    QFBank &bank = access[i];
    if (bank.IsLocked()) continue;
    if (!bank.CanBeUnloaded()) continue;
    LogF("Unloading bank %s",(const char *)bank.GetPrefix());
    bank.Unload();
    return true;
  }
  // if no unlocked bank is available for unloading, try locked banks
  for (int i=0; i<access.Size(); i++)
  {
    QFBank &bank = access[i];
    //if (bank.IsLocked()) continue;
    if (!bank.CanBeUnloaded()) continue;
    LogF("Unloading locked bank %s",(const char *)bank.GetPrefix());
    bank.Unload();
    return true;
  }
  return false;
}

int BankList::Add(QFBank *newBank)
{
  _orderDirty = true;
  return _banks.Add(newBank);
}

BankList::BankList()
{
  _orderDirty = false;
}

RString BankList::LoadBank(RString filename, RString prefix)
{
  QFBank *bank = new QFBank;
  bank->open(filename);
  prefix.Lower();
  bank->SetPrefix(prefix);
  BankList::WriteAccess access(GFileBanks);
  Add(bank);
  return prefix;
}
void QFBankQueryFunctions::ClearBanks()
{
  BankList::WriteAccess access(GFileBanks);
  access.Clear();
}


QFBank *QFBankQueryFunctions::AutoBank( const char *name )
{
  if( !*name ) return NULL;
  if( name[1]==':' ) return NULL;

  #if 0
  ReadAccess access(GFileBanks);
  QFBank *match = GFileBanks.FindBank(name);
  if (!match)
  {
    BString<512> longPath;
    strcpy(longPath,name);
    for(;;)
    {
      // try shorter and shorter path
      LString lstr = strrchr(longPath,'\\');
      if (lstr<=longPath) break;
      // shorten one step and try it
      Assert (lstr[0]!=0);
      lstr[1] = 0; // cut the name (after the backslash)
      QFBank *match = GFileBanks.FindBank(longPath);
      if (match)
        return match;
      lstr[0] = 0; // cut the backslash
    }
  }
  
  #else
  
  BankList::ReadAccess access(GFileBanks);
  // select bank with the longest prefix
  QFBank *bestBank = NULL;
  int maxLen = 0;
  for (int i=0; i<access.Size(); i++)
  {
    QFBank &bank = access[i];
    const RString &prefix = bank.GetPrefix();
    if (!CmpStartStr(name, prefix))
    {
      int len = prefix.GetLength();
      if (len > maxLen)
      {
        bestBank = &bank;
        maxLen = len;
      }
    }
  }
  QFBank *match = bestBank;
//   DoAssert(bestBank==match);
//   
//   match = bestBank;
  #endif
  return match;
}

QIFStreamB::QIFStreamB()
{
}

QFBankHandle QFBankQueryFunctions::GetHandle( const char *name, QFileSize offset, QFileSize size)
{
  const char *name0=name;
  if( GUseFileBanks )
  {
    QFBank *bank=AutoBank(name);
    if( bank )
    {
      // check if we should use bank version of the file
      // skip bank name
      name += bank->GetPrefix().GetLength();
      QFBankHandle handle = bank->GetHandle(name0,name,offset,size);
      #if !_ENABLE_INJECTING
      return handle;
      #else
      if (!GEnablePatching) return handle;
      if( !handle.IsNull())
      {
        return handle;
      }
      // if file does not exist in bank, try to open it from file
      LogF("File %s not in bank",name0);
      #endif
    }
  }
  return new QFBankPointerToAny(name0,offset,size);

  //QIFStream::open(name0,offset,size);
}

Ref<IFileBuffer> QIFStreamB::OpenFromBank(const QFBankHandle &handle)
{
  return handle.OpenSource();
}

Ref<IFileBuffer> QIFStreamB::OpenSource(const char *name, QFileSize offset, QFileSize size)
{
  const char *name0=name;
  if( GUseFileBanks )
  {
    QFBank *bank=QFBankQueryFunctions::AutoBank(name);
    if( bank )
    {
      // check if we should use bank version of the file
      // skip bank name
      name += bank->GetPrefix().GetLength();
      Ref<IFileBuffer> buffer = OpenFromBank(*bank,name,offset,size);
      // how can we check if file is OK?
      if (buffer)
      {
        return buffer;
      }
      // if file does not exist in bank, try to open it from file
      #if !_ENABLE_INJECTING
        return NULL;
      #else
        if (!GEnablePatching) return NULL;
      LogF("File %s not in bank",name0);
      #endif
    }
  }
  return QIFStream::OpenSource(name0,offset,size);

  }

void QIFStreamB::AutoOpen(const char *name, QFileSize offset, QFileSize size)
{
  PROFILE_SCOPE_EX(qfAOp,file);
  Ref<IFileBuffer> source = OpenSource(name,offset,size);
  OpenBuffer(source);
}

QFileTime QFBankQueryFunctions::TimeStamp( const char *name, IQFBankContext *context )
{
  if( GUseFileBanks )
  {
    QFBank *bank=AutoBank(name);
    if( bank )
    {
        const char *rName = name + bank->GetPrefix().GetLength();
        if (context && !context->IsAccessible(bank))
        {
          if( bank->FileExists(rName) ) return 0;
          LogF("FileExist %s: access denied",name);
          return 0;
        }
        const FileInfoO &info = bank->FindFileInfo(rName);
        if (!QFBank::IsNull(info))
        {
          #if _ENABLE_PATCHING
          if (!info.loadFromFile)
          #endif
          {
            return info.time;
          }
        }
        else
        {
          #if !_ENABLE_INJECTING
          return 0;
          #endif
          if (!GEnablePatching) return 0;
        }
    }
  }
  return QIFileFunctions::TimeStamp(name);
}

/**
@return
negative when A should be loaded sooner then B,
positive when A should be loaded later then B
*/
int QFBankQueryFunctions::FileOrder(const char *nameA, const char *nameB)
{
  QFBank *bA = QFBankQueryFunctions::AutoBank(nameA);
  QFBank *bB = QFBankQueryFunctions::AutoBank(nameB);
  if (bA<bB) return -1;
  if (bA>bB) return +1;
  Assert(bA==bB);
  if (!bA) return 0;
  int oA = bA->GetFileOrder(nameA+strlen(bA->GetPrefix()));
  int oB = bB->GetFileOrder(nameB+strlen(bB->GetPrefix()));
  return oA-oB;
}

bool QFBankQueryFunctions::FileExist( const char *name, IQFBankContext *context)
{
  if( GUseFileBanks )
  {
    QFBank *bank=AutoBank(name);
    if( bank )
    {
      if (context && !context->IsAccessible(bank))
      {
        const char *rName = name + bank->GetPrefix().GetLength();
        if( bank->FileExists(rName) ) return false;
        LogF("FileExist %s: access denied",name);
        return false;
      }
      const char *rName = name + bank->GetPrefix().GetLength();
      if( bank->FileExists(rName) ) return true;
      #if !_ENABLE_INJECTING
      return false;
      #else
      if (!GEnablePatching) return false;
      #endif
    }
  }
  return QIFileFunctions::FileExists(name);
}

QFileSize QFBankQueryFunctions::GetFileSize(const char *name)
{
  if( GUseFileBanks )
  {
    QFBank *bank=AutoBank(name);
    if( bank )
    {
      /*
      if (context && !context->IsAccessible(bank))
      {
        const char *rName = name + bank->GetPrefix().GetLength();
        if( bank->FileExists(rName) ) return false;
        LogF("FileExist %s: access denied",name);
        return false;
      }
      */
      const char *rName = name + bank->GetPrefix().GetLength();
      const FileInfoO &info = bank->FindFileInfo(rName);
      if (bank->NotNull(info))
      {
        #if _ENABLE_PATCHING
        if (!info.loadFromFile)
        #endif
        {
          return info.compressedMagic!=0 ? info.uncompressedSize : info.length;
        }
      }
      else
      {
        #if !_ENABLE_INJECTING
        return 0;
        #else
        if (!GEnablePatching) return 0;
        #endif
      }
    }
  }
  return QIFileFunctions::GetFileSize(name);
}

bool QFBankQueryFunctions::FileIsTakenFromBank(const char *name)
{
  if( GUseFileBanks )
  {
    QFBank *bank=AutoBank(name);
    if( bank )
    {
      const char *rName = name + bank->GetPrefix().GetLength();
      const FileInfoO &info = bank->FindFileInfo(rName);
      if (bank->NotNull(info))
      {
#if _ENABLE_PATCHING
        // file is patched
        if (GEnablePatching && info.loadFromFile)
          return false;
#endif
        return true;
      }
    }
  }
  return false;
}

struct EncryptorInformation
{
  RString name;
  IFilebankEncryption *(*createFunction)(const void *context);
};

TypeIsMovableZeroed(EncryptorInformation)

template <>
struct FindArrayKeyTraits<EncryptorInformation>
{
  typedef const char *KeyType;
  static bool IsEqual(const char * a, const char *b)
  {
    return !strcmpi(a,b);
  }
  static const char *GetKey(const EncryptorInformation &a) {return a.name;}
};

static FindArrayKey<EncryptorInformation> GEncryptors;

void RegisterFilebankEncryption
(
  const char *name, IFilebankEncryption *(*createFunction)(const void *context)
)
{
  // check if given encyption already exists
  int index = GEncryptors.FindKey(name);
  if (index>=0)
  {
    ErrF("Ecryption %s already registered",name);
    return;
  }
  EncryptorInformation &ei = GEncryptors.Append();
  ei.name = name;
  ei.createFunction = createFunction;
}

Ref<IFilebankEncryption> CreateFilebankEncryption(const char *name, const void *context)
{
  int index = GEncryptors.FindKey(name);
  if (index<0)
  {
    ErrF("Unknown encryption %s",name);
    return NULL;
  }
  return GEncryptors[index].createFunction(context);
}

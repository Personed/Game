#include <El/elementpch.hpp>

#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <time.h>

#include <sys/stat.h>
#include <Es/Files/filenames.hpp>
#include <Es/Files/fileContainer.hpp>
#ifndef _WIN32
#define __cdecl
#include <unistd.h> 
#include <dirent.h> 
#else
#include <io.h>
#endif

#include <Es/Containers/staticArray.hpp>
#include <Es/Algorithms/qsort.hpp>
#include <El/QStream/qStream.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/QStream/packFiles.hpp>

#include <stdarg.h>

#if _ENABLE_PBO_PROTECTION
#include <El/Encryption/PBOHeaderProtection.h>
#endif
// use LZW compression on files of some types (e.g. .p3d)

#ifndef _WIN32
#define DIR_STR "/"
#define DIR_CHR '/'
#else
#define DIR_STR "\\"
#define DIR_CHR '\\'
#endif


// save FileInfo to stream

void LoadFileInfo( FileInfoExt &i, QIStream &f )
{
  // read zero terminated name
  char name[256];
  char *n=name;
  int maxLen=sizeof(name)-1;
  for (int l=0; l<maxLen; l++)
  {
    char c = f.get();
    if (!c) break;
    *n++=c;
  }
  *n=0; // zero terminate in any case
  i.name = name;
  f.read(&i.compressedMagic,sizeof(i.compressedMagic));
  f.read(&i.uncompressedSize,sizeof(i.uncompressedSize));
  f.read(&i.startOffset,sizeof(i.startOffset));
  f.read(&i.time,sizeof(i.time));
  f.read(&i.length,sizeof(i.length));
}

void SaveFileInfo( const FileInfoExt &i, QOStream &f )
{
  // save zero terminate name
  f.write((const char *)i.name,i.name.GetLength()+1);
  f.write(&i.compressedMagic,sizeof(i.compressedMagic));
  f.write(&i.uncompressedSize,sizeof(i.uncompressedSize));
  f.write(&i.startOffset,sizeof(i.startOffset));
#if _VBS2
  unsigned long zero = 0; // files have no time. when packing
  f.write(&zero,sizeof(zero));
#else
  f.write(&i.time,sizeof(i.time));
#endif
  f.write(&i.length,sizeof(i.length));
}

void QOFStreamChecked::InitChecker(bool reversed)
{
  _reversed = reversed;
  QOFStream::Flush(0);
  _calculator.Reset();
}

void QOFStreamChecked::Flush(size_t size)
{
  const char *data = (const char *)_buf->DataLock(0, _bufferSize);
  _calculator.Add(data, _bufferSize);
  _buf->DataUnlock(0, _bufferSize);
  QOFStream::Flush(size);
}

static int MatchMask(const char *mask, const char *string)
{
  bool exclude = false;
  if (*mask=='~')
  {
    exclude = true;
    mask++;
  }
  int ret = Matches(string,mask);
  return exclude ? -ret : ret;
}

void FileBankManager::ParseMasks(const char *logfile)
{
  // set priority to all files
  FILE *f = fopen(logfile,"r");
  if (!f) return;

  int line = 1;
  for(;;)
  {
    // scan rule
    char buf[1024];
    *buf = 0;
    fgets(buf,sizeof(buf),f);
    if (!*buf) break;
    if (strlen(buf)>0)
    {
      if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1] = 0;
    }
    // buf is now wildcard mask
    // try to match this mask with all files
    for (int i=0; i<_files.Size(); i++)
    {
      FileInfoExt &fi = _files[i];
      int match = MatchMask(buf,fi.name);
      if (match==0) continue;
      if (match>0)
      {
        fi.priority = line;
      }
      // break;
    }
    line++;
  }
  fclose(f);
}

static int CompareFileInfo( const FileInfoExt *f1, const FileInfoExt *f2 )
{
  int d = f1->priority - f2->priority;
  if (d) return d;
  return strcmpi(f1->name,f2->name);
}


static inline bool MatchExcludeList(const char *filename, const RString *exclude, int nExclude)
{
  for (int i=0; i<nExclude; i++)
  {
    int ret = MatchMask(exclude[i],filename);
    if (ret) return ret>0;
  }
  return false;
}

void FileBankManager::Exclude(const RString *exclude, int nExcludes)
{
  int src,dst;
  for (src=0,dst=0; src<_files.Size(); src++)
  {
    if (MatchExcludeList(_files[src].name,exclude,nExcludes))
    {
      continue;
    }
    if (src!=dst)
    {
      Assert(dst<src);
      _files[dst]=_files[src];
    }
    dst++;
  }
  _files.Resize(dst);
}

#if _ENABLE_PBO_PROTECTION
void FileBankManager::ExcludeDummyPaddingFile()
{
  for (int i=0; i<_files.Size(); ++i)
  {
    if (stricmp(_files[i].name.Data(), PBOHeaderProtection::DummyPaddingFileName) == 0)
    {
      // exclude dummy file
      _files.Delete(i);
      break;
    }
  }
}

#endif //#if _ENABLE_PBO_PROTECTION

void FileBankManager::SortAndRemove(bool remove)
{
  // sort files by priority - then name
  QSort(_files.Data(),_files.Size(),CompareFileInfo);

  if (remove)
  {
    // remove files with negative priority
    int firstIPositive = _files.Size();
    for (int i=0; i<_files.Size(); i++)
    {
      if (_files[i].priority<=0) continue;
      firstIPositive=i;
      break;
    }
    if (firstIPositive>0)
    {
      for (int i=firstIPositive; i<_files.Size(); i++)
      {
        _files[i-firstIPositive] = _files[i];
      }
      _files.Resize(_files.Size()-firstIPositive);
    }
  }
  FinishFileList();  
}

void FileBankManager::FinishFileList()
{
  for (int i=0; i<_files.Size(); i++)
  {
    _size += _files[i].length;
  }

  // add terminator
  FileInfoExt f;
  f.Zero();
  f.priority = 0;
  _files.Add(f);
}

void FileBankManager::ScanDir(RString dir, RString rel)
{
  #ifndef _WIN32
  DIR *scan=OpenDir(dir);
  if( scan )
  {
    struct dirent *entry;
    while( (entry=readdir(scan))!=NULL )
    {
      const char *name=entry->d_name;
      RString subdir=dir+RString(DIR_STR)+RString(name);
      RString subrel=rel+RString(DIR_STR)+RString(name);
      struct stat buf;
      if( stat(subdir,&buf)<0 )
      {
        LogF("Error %s\n",(const char *)subdir);
        continue;
      }
      if( buf.st_mode&S_IFDIR )
      {
        if( *name=='.' ) continue;
        ScanDir(subdir,subrel);
        continue;
      }
      FileInfoExt f;
      const char *rc=subrel;
      if( *rc==DIR_CHR ) rc++;
      memset(&f,0,sizeof(f));
      strncpy((char*)(const char*)f.name,rc,sizeof(f.name));
      f.length=buf.st_size;
      f.time=buf.st_mtime;
      if( _newestFile<f.time ) _newestFile=f.time;
      //size+=f.length;
      _files.Add(f);
    }
    closedir(scan);
  }
  #else
  _finddata_t fInfo;
  RString wild=RString(dir)+RString("\\*");
  long hFile=X_findfirst(wild,&fInfo);
  if( hFile!=-1 )
  {
    do
    {
      RString subrel=rel+RString(DIR_STR)+RString(fInfo.name);
      if( fInfo.attrib&_A_SUBDIR )
      {
        if( fInfo.name[0]=='.' ) continue;
        RString subdir=dir+RString(DIR_STR)+RString(fInfo.name);
        ScanDir(subdir,subrel);
        continue;
      }
      FileInfoExt f;
      const char *rc=subrel;
      if( *rc==DIR_CHR ) rc++;
      memset(&f,0,sizeof(f));
      f.name = rc;
      f.length=fInfo.size;
      f.time=fInfo.time_write;
      f.compressedMagic = 0;
      f.uncompressedSize = 0;
      if( _newestFile<f.time ) _newestFile=f.time;
      //size+=f.length;
      _files.Add(f);
    }
    while( _findnext(hFile,&fInfo)==0 );
    _findclose( hFile );
  }
  #endif
}


#if _ENABLE_PBO_PROTECTION
QFileSize FileBankManager::SaveHeadersOptProtected( QOStream &out, IFilebankEncryption* headerProtection)
{
  Assert(headerProtection);
  if (!headerProtection)
    return 0;

  // create unencrypted headers
  QOStrStream headers;
  SaveHeadersOpt(headers, HPNone, 0);

  return PBOHeaderProtection::EncryptHeaders(out, headers, headerProtection);
}
#endif //#if _ENABLE_PBO_PROTECTION 

QFileSize FileBankManager::SaveHeadersOpt( QOStream &out, EHeaderProtectionType headerProtectionType, IFilebankEncryption* headerProtection)
{
  QFileSize startPos = out.tellp();
  switch (headerProtectionType)
  {
  case HPNone:
    // no protection
    // just save header normally
  for( int i=0; i<_files.Size(); i++ )
  {
    const FileInfoExt &file=_files[i];
    SaveFileInfo(file,out);
  }
    break;
#if _ENABLE_PBO_PROTECTION
  case HPSetupPrepare:
    // prepare for setup encryption
    // do not encrypt, just prepare space for encrypted header
    {
      Assert(headerProtection);

      // save normal headers to separate stream
      QOStrStream headers;

      // create dummy file info
      FileInfoExt dummyFileInfo;
      dummyFileInfo.name = PBOHeaderProtection::DummyPaddingFileName;
      dummyFileInfo.compressedMagic = 0;
      dummyFileInfo.length = 0; //correct length will be computed later
      dummyFileInfo.startOffset = 0; //offset is not important, will be recomputed when opening PBO
      dummyFileInfo.time = 0;
      dummyFileInfo.uncompressedSize = 0;

      // save dummy file info to stream
      SaveFileInfo(dummyFileInfo, headers);
      // save rest of the headers
      SaveHeadersOpt(headers, HPNone, 0);

      // we will create protected headers just to get the size of protected headers
      QOStrStream protHeaders;
      PBOHeaderProtection::EncryptHeaders(protHeaders, headers, headerProtection);

      // compute needed padding
      int padding = protHeaders.pcount() - headers.pcount();
      Assert(padding > 0);

      // write dummy file info with correct length
      dummyFileInfo.length = padding;
      headers.seekp(0, QIOS::beg);
      SaveFileInfo(dummyFileInfo, headers);

      // write normal headers to output
      out.write(headers.str(), headers.pcount());

      // write padding to output
      PBOHeaderProtection::SaveDummyPaddingFile(out, padding);
}

    break;
  case HPSetupProtect:
    // encrypt prepared headers
    {
      Assert(headerProtection);
      SaveHeadersOptProtected(out, headerProtection);
    }
    break;
#endif //#if _ENABLE_PBO_PROTECTION
  default:
    Assert(!"Invalid header protection type");
  }//switch

  return out.tellp() - startPos;
}

void FileBankManager::SaveProperties( QOStream &out, const QFProperty *prop, int nProp )
{
  if (prop && nProp>0)
  {
    // save terminator
    FileInfoExt terminator;
    terminator.Zero();
    terminator.compressedMagic = VersionMagic;
    SaveFileInfo(terminator,out);
    for (int i=0; i<nProp; i++)
    {
      out.write((const char *)prop->name,prop->name.GetLength()+1);
      out.write((const char *)prop->value,prop->value.GetLength()+1);
      //! Added - Increase the property pointer..
      prop++;
    }
    out.write("",1);
  }
}

FileBankManager::FileBankManager()
{
  _newestFile=0;
  _size=0;
}

FileBankManager::~FileBankManager()
{
}

const char *DefFileBankNoCompress[]=
{
  ".pbo",
  ".ogg",".wss",
  ".jpg",
  NULL
};

const char *DefFileBankEncrypt[]=
{
  ".p3d",
  ".bin",".cpp",
  ".sqm",".sqs",".ext",".csv",
#if _VBS2 && (_VBS2_LITE || _VBS2_ENC)
  ".sqf",".fsm",".log",".hpp",".h",".c",".html",
#endif
  NULL
};

static bool StringInList(const char *str, const char **list)
{
  while (*list)
  {
    if (!strcmpi(*list,str)) return true;
    list++;
  }
  return false;
}

void FileBankManager::PrepareFileList(
  RString folder, const char *logFile,
  const RString *exclude, int nExcludes
)
{
  _newestFile = 0;
  _size = 0;

  _files.Resize(0);
  _files.Realloc(1024);
  ScanDir(folder,"");

  DoAssert(_files.Size()>0);

  if (logFile)
  {
    ParseMasks(logFile);
  }
  if (exclude && nExcludes>0)
  {
    Exclude(exclude,nExcludes);
  }
#if _ENABLE_PBO_PROTECTION
  ExcludeDummyPaddingFile();
#endif
  SortAndRemove(logFile != NULL);
}

void FileBankManager::StoreFiles
(
  QOStream &out, RString folder,
  bool compress, const char **doNotCompress,
  const QFProperty *properties, int nProperties,IBankChecker *checker, IFilebankEncryption *headerProtection
)
{
  if( !compress )
  {
    // save headers
    out.setbuffer(1024*1024);
    if (checker) checker->InitChecker(false);
    SaveProperties(out,properties,nProperties);
    SaveHeadersOpt(out, headerProtection? HPSetupPrepare : HPNone, headerProtection);
    for( int i=0; i<_files.Size(); i++ )
    {
      FileInfoExt &file=_files[i];
      RString inFilename=folder+RString(DIR_STR)+RString(file.name);
      QIFStream in;
      in.open(inFilename);
      in.copy(out);
#if _VBS3
      in.close();
      GFileServerFunctions->FlushReadHandle(inFilename);
#endif
    }
  } // if( !compress )
  else
  {
    out.setbuffer(_size*3/4+2*1024*1024);
    SaveProperties(out,properties,nProperties);
    SaveHeadersOpt(out, headerProtection? HPSetupPrepare : HPNone, headerProtection);
    if (checker) checker->InitChecker(true);
    for( int i=0; i<_files.Size(); i++ )
    {
      FileInfoExt &file=_files[i];
      RString inFilename=folder+RString(DIR_STR)+RString(file.name);
      QIFStream in;
      in.open(inFilename);
      // check if it should be compressed
      const char *ext = GetFileExt(file.name);
      if (doNotCompress && StringInList(ext,doNotCompress))
      {
        in.copy(out);
      }
      else
      {
        SSCompress ss;
        int offset=out.tellp();
        ss.Encode(out,in);
        file.uncompressedSize=file.length;
        file.length=out.tellp()-offset;
        file.compressedMagic=CompMagic;
      }
#if _VBS3
      in.close();
      GFileServerFunctions->FlushReadHandle(inFilename);
#endif
    }

    // rewind and save headers again
    out.seekp(0,QIOS::beg);
    SaveProperties(out,properties,nProperties);
    SaveHeadersOpt(out, headerProtection? HPSetupPrepare : HPNone, headerProtection);
  } // if( !compress ) else
}

LSError FileBankManager::Create
(
  const char *tgt, const char *src,
  bool compress, bool optimize, const char *logFile,
  const char **doNotCompress,
  const QFProperty *properties, int nProperties,
  const RString *exclude, int nExcludes,IFilebankEncryption *headerProtection
)
{
  Assert(optimize);
  Assert(!compress);
  
  RString folder = src;
  PrepareFileList(folder, logFile, exclude, nExcludes);

  RString target = tgt;

  // repack only if destination is older than all sources
  if (QIFileFunctions::FileExists(target))
  {
  QFileTime destTime=QIFileFunctions::TimeStamp(target);
  if( destTime>=_newestFile )
  {
    LogF
    (
      "%s skipped - no changes detected.\n",
      (const char *)folder
    );
    return LSOK;
  }
  }

  const char *action="Repacking";
  LogF("%s: %s %d files (%d KB).\n",(const char *)folder,action,_files.Size(),(_size+1023)/1024);

  LSError err = LSOK;

  QOFStreamChecked out;
  out.open(target);
  StoreFiles(out, folder, compress, doNotCompress, properties, nProperties, &out, headerProtection);
  bool reversed = out.IsReversed();
  AutoArray<char> result;
  if (!out.GetResult(result)) err = LSUnknownError;

  // do not write before out.GetResult (result can be changed)
  out.write(&reversed, sizeof(reversed));
  out.write(result.Data(), result.Size() * sizeof(char));
  out.close();
  _files.Clear();

  return err;
}

LSError FileBankManager::Create
(
 const char *tgt, const char *src,
 bool compress, bool optimize, const AutoArray<FileInfoExt>& files,
 const char **doNotCompress,
 const QFProperty *properties, int nProperties,IFilebankEncryption *headerProtection
 )
{
  Assert(optimize);
  Assert(!compress);
  RString folder = src;
  RString target = tgt;

  LSError err = LSOK;

  QOFStreamChecked out;
  out.open(target);
  _files.Clear();
  _files.Append(files);
  FinishFileList();
  StoreFiles(out, folder, compress, doNotCompress, properties, nProperties, &out, headerProtection);
  bool reversed = out.IsReversed();
  AutoArray<char> result;
  if (!out.GetResult(result)) err = LSUnknownError;

  // do not write before out.GetResult (result can be changed)
  out.write(&reversed, sizeof(reversed));
  out.write(result.Data(), result.Size() * sizeof(char));
  out.close();
  _files.Clear();

  return err;
}


/*!
\patch 1.45 Date 2/26/2002 by Ondra
- Fixed: OGG and JPG files now not compressed during mission export,
bug resulted in ineffective streaming audio support in custom missions.
*/

void FileBankManager::Create
(
  QOStream &out, const char *src,
  bool compress, bool optimize, const char *logFile,
  const char **doNotCompress,
  const QFProperty *properties, int nProperties,
  const RString *exclude, int nExcludes,IFilebankEncryption *headerProtection
)
{
  Assert (optimize);
  Assert(!compress);
  
  RString folder = src;
  PrepareFileList(folder, logFile, exclude, nExcludes);

  StoreFiles(out, folder, compress, doNotCompress, properties, nProperties, NULL, headerProtection);
  _files.Clear();
}

#if _VBS2
bool FileBankManager::EncryptPbo(RString srcPath,RString dstPath,const char **encryptExts)
{
  // open input bank!
  QFBank bank;
  if(!bank.open(srcPath))
  {
    DoAssert(false);
    return false;
  }
  if(!bank.Load())
  {
    LogF("Failed to load bank");
    return false;
  }
  // open output stream!
  QOFStream out;
  out.open(dstPath);
  if(out.fail())
  {
    DoAssert(false);
    return false;
  }
  out.setbuffer(1024*1024*16);

  // can be nothing inside properties
  AutoArray<QFProperty> properties = bank._properties;
    
  QFProperty vbs2Enc;
  vbs2Enc.name = RString("encryption");
  vbs2Enc.value = RString("vbs2");  
  properties.Add(vbs2Enc);

  vbs2Enc.name = RString("prefix");
  vbs2Enc.value = bank.GetPrefix();
  if(vbs2Enc.value.GetLength()>1)
    vbs2Enc.value = RString(vbs2Enc.value,vbs2Enc.value.GetLength()-1);
  properties.Add(vbs2Enc);

  LogF("prefix:%s",(const char*)vbs2Enc.value);


  // read the files avaiable in the bank
  FileBankType bankFiles = bank._files;
  AutoArray<FileInfoExt> files;
  for (int i=0; i<bankFiles.NTables(); i++)
  {
    AutoArray<FileInfoO> &container = bankFiles.GetTable(i);
    for (int j=0; j<container.Size(); j++)
    {
      int index = files.Add();
      files[index].name = container[j].name;
      files[index].compressedMagic = container[j].compressedMagic;
      files[index].uncompressedSize = container[j].uncompressedSize;
      files[index].startOffset = container[j].startOffset;
      files[index].time = container[j].time;
      files[index].length = container[j].length;
    }
  }

  // Create VBS2 Encryption
  Ref<IFilebankEncryption> encrypt = CreateFilebankEncryption("VBS2","password");
  if(!encrypt)
  {
    DoAssert(false);
    return false;
  }

  QOStrStream tmpStream;
  tmpStream.setbuffer(1024*1024*256);

  // allocate the necceary buffer, open the file and read in the data
  // then write out the data like normal.
  for( int i = 0; i < files.Size(); i++ )
  {
    const char *ext = GetFileExt((const char*) files[i].name);

    // read in the whole file
    Ref<IFileBuffer> buffer = bank.Read( (const char*)files[i].name,0);
    AutoArray<char> data;

    data.Resize(buffer->GetSize());
    buffer->Read(data.Data(),0,data.Size());

    if (!StringInList(ext,encryptExts))
    {
      tmpStream.write(data.Data(),files[i].length);
    }
    else
    {  
      int offset=tmpStream.tellp();
      unsigned char inVec[16] = { 
         0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
         0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c  
       };

       int *ptr = (int*) inVec;
       for( int x = 0; x < 16; x += sizeof(int),++ptr)
          *ptr = files[i].length; // keep appending length to InVec

       encrypt->Encode(tmpStream,data.Data(),data.Size(),inVec);
       files[i].uncompressedSize=files[i].length;
       files[i].length=tmpStream.tellp()-offset;
       files[i].compressedMagic=EncrMagic;
    }
  }
  

  SaveProperties(out,properties.Data(),properties.Size());
  QOStrStream headers;
  for( int i = 0; i < files.Size(); ++i)
    SaveFileInfo(files[i],headers);
  
  unsigned char inVec[16] = { 
    0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
    0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c 
  };
          
  if(tmpStream.pcount()<16)
  {
    Fail("Data size too small for invec");
  }
  else
  {
    memcpy(inVec,tmpStream.str(),sizeof(inVec));
  }

  QOStrStream headersEncoded;
  encrypt->Encode(headersEncoded,headers.str(),headers.pcount(),inVec);

  int headersSize = headers.pcount();
  int headersEncodedSize = headersEncoded.pcount();

  out.write(&headersSize,sizeof(headersSize));
  out.write(&headersEncodedSize,sizeof(headersEncodedSize));
  out.write(headersEncoded.str(),headersEncoded.pcount());
  out.write(tmpStream.str(),tmpStream.pcount());
  out.close();
  return true;
}
#endif


void FileBankManager::Create
(
  QOStream &out, const char *src, 
  IFilebankEncryption *encrypt,
  const QFProperty *properties, int nProperties,
  const RString *exclude, int nExcludes,
  const char **encryptExts
)
{
  RString folder = src;
  PrepareFileList(folder, NULL, exclude, nExcludes);
  
  QOStrStream temp;
  temp.setbuffer(1024*1024*128);

#if _VBS2 && (_VBS2_LITE || _VBS2_ENC)
  if(_files.Size()==0) // nothing to pack!
    return;

  bool vbs2Enc = false;
  for( int ix = 0; ix < nProperties; ix++ )
    if( strcmpi((const char*)properties[ix].value,"VBS2")==0 ||
        strcmpi((const char*)properties[ix].value,"VBS2M")==0 )
    {
      vbs2Enc = true;
      break;
    }

  if(!vbs2Enc) // dont perform any other encryption other than vbs2
    return;
#endif

  for( int i=0; i<_files.Size(); i++ )
  {
    FileInfoExt &file=_files[i];
    RString inFilename=folder+RString(DIR_STR)+RString(file.name);
    QIFStream in;
    in.open(inFilename);
    // check if it should be compressed
    const char *ext = GetFileExt(file.name);
    if (encryptExts && !StringInList(ext,encryptExts))
    {
      in.copy(temp);
    }
    else
    {
      LogF("Encrypting %s",(const char *)file.name);
#if _VBS2 && (_VBS2_LITE || _VBS2_ENC)
     if(file.length==0){ in.copy(temp); continue; }
      
      int offset=temp.tellp();
     unsigned char inVec[16] = {  
       0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
       0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c  
     };

     int *ptr = (int*) inVec;
     for( int x = 0; x < 16; x += sizeof(int),++ptr)
        *ptr = file.length; // keep appending length to InVec

     encrypt->Encode(temp,in,inVec);
     file.uncompressedSize=file.length;
     file.length=temp.tellp()-offset;
     file.compressedMagic=EncrMagic;
#else
     int offset=temp.tellp();
      encrypt->Encode(temp,in);
      file.uncompressedSize=file.length;
      file.length=temp.tellp()-offset;
     // Note: if the engine doesnt read this, it performs like a normal file I/O
      file.compressedMagic=EncrMagic;
#endif
    }
  }

  // save properties
  SaveProperties(out,properties,nProperties);
  // save headers
  QOStrStream headers;
  SaveHeadersOpt(headers, HPNone, 0);
  QOStrStream headersEncoded;
  // encrypte headers
#if _VBS2 && (_VBS2_LITE || _VBS2_ENC)
  // random inilisation vector.
  unsigned char inVec[16] = { 
    0x4b,0x64,0x6b,0x41,0x4c,0x40,0x61,0x4f,
    0x6f,0x79,0x30,0x3a,0x7b,0x65,0x20,0x6c 
  };
          
  if(temp.pcount()<16)
    Fail("Data size too small for invec");
  else
    memcpy(inVec,(void*)temp.str(),sizeof(inVec));// Read true inilisation vector from the data itself!
  
  encrypt->Encode(headersEncoded,headers.str(),headers.pcount(),inVec);
#else
  encrypt->Encode(headersEncoded,headers.str(),headers.pcount());
#endif

  int headersSize = headers.pcount();
  int headersEncodedSize = headersEncoded.pcount();
  // save headers decrypted size
  out.write(&headersSize,sizeof(headersSize));
  // save headers encrypted size
  out.write(&headersEncodedSize,sizeof(headersEncodedSize));
  // save encrypted headers
  out.write(headersEncoded.str(),headersEncoded.pcount());
  // save all encypted content
  out.write(temp.str(),temp.pcount());
  _files.Clear();
}

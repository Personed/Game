// binarize.cpp : Defines the entry point for the console application.
//
#include "../lib/wpch.hpp"

#if _ENABLE_SINGLE_BINARIZE_EXECUTABLE

#include "../posAccess/posAccess.hpp"
#include "../lib/Shape/shape.hpp"
#include "../lib/Shape/modelConfig.hpp"
#include "../lib/landscape.hpp"
#include "../lib/engine.hpp"
#include "../lib/engDummy.hpp"
#include "../lib/world.hpp"
#include <El/FileServer/fileServer.hpp>
#include "../lib/depMake.hpp"
#include "../lib/rtAnimation.hpp"
#include "../lib/keyInput.hpp"

#include <Es/Files/fileNames.hpp>
#include <Es/Files/fileContainer.hpp>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <Es/common/win.h>
#include <Es/Containers/rStringArray.hpp>
#include <El/FileBinMake/fileBinMake.hpp>
#include <El/paramfile/paramfile.hpp>
#include "../lib/appFrameExt.hpp"
#include "../lib/Network/network.hpp"

class BinarizeFrameFunctions : public OFPFrameFunctions
{
public:
  BinarizeFrameFunctions() : OFPFrameFunctions() {}
  void ErrorMessage(const char *format, va_list argptr) override;
  void WarningMessage( const char *format, va_list argptr) override;
  void ErrorMessage(ErrorMessageLevel level, const char *format, va_list argptr) override;
#if _ENABLE_REPORT
  void LogF( const char *format, va_list argptr) override;
#endif
  void LstFDebugOnly(const char *format, va_list argptr) override;
  void LstF( const char *format, va_list argptr) override;
  void ErrF( const char *format, va_list argptr) override;
};

void BinarizeFrameFunctions::ErrorMessage(const char *format, va_list argptr)
{
  RptF(format,argptr);
  #if _DEBUG
    __asm {int 3}
  #endif
}

void BinarizeFrameFunctions::WarningMessage( const char *format, va_list argptr)
{
  RptF(format,argptr);
  #if _DEBUG
    __asm {int 3}
  #endif
}

void BinarizeFrameFunctions::ErrorMessage(ErrorMessageLevel level, const char *format, va_list argptr)
{
  RptF(format,argptr);
  #if _DEBUG
    //__asm {int 3}
  #endif
}

#if _ENABLE_REPORT
void BinarizeFrameFunctions::LogF( const char *format, va_list argptr)
{
  BString<512> buf;
    
  vsprintf(buf,format,argptr);
  strcat(buf,"\n");
  OutputDebugString(buf);
  //printf(buf);
}
#endif

void BinarizeFrameFunctions::ErrF( const char *format, va_list argptr)
{
  BString<512> buf;
    
  vsprintf(buf,format,argptr);
  strcat(buf,"\n");
  OutputDebugString(buf);
  fprintf(stderr,buf);
}

void BinarizeFrameFunctions::LstF( const char *format, va_list argptr)
{
  BString<512> buf;
    
  vsprintf(buf,format,argptr);
  strcat(buf,"\n");
  OutputDebugString(buf);
  fprintf(stderr,buf);
}

void BinarizeFrameFunctions::LstFDebugOnly(const char *format, va_list argptr)
{
  LstF(format, argptr);
}

static BinarizeFrameFunctions GBinarizeFrameFunctions INIT_PRIORITY_URGENT;

extern ParamFile Pars;
extern bool GShrinkP3D;

void NormalizePathRString(RString& path)
{
  int length = path.GetLength();
  if (0 == length)
    return;

  char *str = new char[path.GetLength()+1];
  strcpy(str, path.Data());
  NormalizePath(str);
  path = str;
  delete[] str;
}

const char *GetCWD()
{
  static char cwd[1024];
  getcwd(cwd,sizeof(cwd));
  return cwd;
}

static void MakeAllDirs(const char *path)
{
  const char *delim = strchr(path,':');
  if (!delim) delim = path;
  delim = strchr(delim+1,'\\');
  do
  {
    char parent[1024];
    strncpy(parent,path,delim-path);
    parent[delim-path] = 0;
    mkdir(parent);
    delim = strchr(delim+1,'\\');
  } while(delim);
}

static const char *RestArg(const char *a, const char *b)
{
  int l = strlen(b);
  if (strnicmp(a,b,l)) return NULL;
  return a+l;
}

static time_t FileTimestamp(const char *name)
{
  return QFBankQueryFunctions::TimeStamp(name);
}

static bool CheckUpToDate(time_t stime,const char *dst)
{
  return stime<=FileTimestamp(dst);
}

template <>
struct FindArrayKeyTraits<FileInfoO>
{
  typedef const char *KeyType;
  static bool IsEqual(KeyType a, KeyType b)
  {
    return !strcmpi(a,b);
  }
  static KeyType GetKey(const FileInfoO &a) {return a.name;}
};


extern bool UseGlide;
extern bool LandEditor;

struct TextureInfo
{
  RStringB name;
  int time;
  TextureInfo(RStringB n, int t)
  {
    name = n;
    time = t;
  }
};

TypeIsMovableZeroed(TextureInfo)

template <>
struct FindArrayKeyTraits<TextureInfo>
{
  typedef RStringB KeyType;
  static bool IsEqual(const KeyType &a, const KeyType &b)
  {
    return a==b;
  }
  static const KeyType &GetKey(const TextureInfo &a) {return a.name;}
};

#include "../lib/Shape/material.hpp"

/// check if file is up to date, consider dependencies as well
static bool CheckUpToDateWithDep(
  FindArrayKey<TextureInfo> &textures,
  time_t stime,const char *dst,
  const char *texturePath
)
{
  time_t objTime = FileTimestamp(dst);
  if (stime>objTime)
  {
    return false;
  }
  // read .dep file
  QIFStream dep;
  BString<512> depName;
  strcpy(depName,dst);
  strcat(depName,".dep");
  dep.open(depName);
  if (dep.fail())
  {
    // dep file missing - we need to make
    return false;
  }
  // check all links and sources

  // read single filename
  char line[1024];
  while (dep.readLine(line,sizeof(line)))
  {
    if (*line==0 || *line==';') continue;
    const char *filename = line;
    bool isSource = false;
    bool isLink = true;
    bool wasMissing = false;
    if (*line=='+')
    {
      // this line marks source only
      // no need to transfer the file
      filename = line+1;
      isLink = false;
      isSource = true;
    }
    else if (*line=='*')
    {
      filename = line+1;
      isSource = true;
    }
    else if (*line=='-')
    {
      filename = line+1;
      isLink = false;
      isSource = true;
      wasMissing = true;
    }
    
    time_t srcTime = FileTimestamp(filename);
    if (isLink && *texturePath)
    {
      BString<512> dstTexture;
      strcpy(dstTexture,texturePath);
      strcat(dstTexture,filename);
      time_t dstTime = FileTimestamp(dstTexture);
      if (srcTime>dstTime || dstTime==0)
      {
        // link is newer than the destination - it needs updating
        textures.AddUnique(TextureInfo(filename,srcTime));
        if (isSource)
        {
          return false;
        }
      }
    }
    if (isSource)
    {
      // source may cause object target updating
      if (!wasMissing)
      {
        // if if is newer now or it is missing now, rebuild
        if (srcTime==0 || srcTime>objTime)
        {
          return false;
        }
      }
      else
      {
        // if was missing, but it is there now - rebuild
        if (srcTime!=0)
        {
          return false;
        }
      }
    }
  }
  return true;
}

/// reads given file with texture names, adds textures into FindArrayKey array textures
static bool ProcessNotLinkedTextures(
  FindArrayKey<TextureInfo> &textures, 
  const char *txtLstFileName,
  const char *texturePath
)
{
  // read .lst file 
  QIFStream txtLstFile;
  txtLstFile.open(txtLstFileName);
  if (txtLstFile.fail())
  {
    //cannot open texture list file
    return false;
  }
  
  char line[1024];

  
  while (txtLstFile.readLine(line,sizeof(line)) || *line)
  {
    if (*line ==0) continue;    
    const char *filename = line;
    Ref<Texture> tex;
	
	  errno_t err;
	  err = _strlwr_s(line, 1024);

    const char *ext = GetFileExt(GetFilenameExt(line));
    if (!stricmp(ext,".rvmat")) GTexMaterialBank.New(TexMaterialName(line));

    tex = GlobLoadTexture(GlobResolveTexture(line));
    filename = tex->Name();
   
    time_t srcTime = FileTimestamp(filename);    

    if (*texturePath)
    {
      BString<512> dstTexture;
      strcpy(dstTexture,texturePath);
      strcat(dstTexture,filename);
      time_t dstTime = FileTimestamp(dstTexture);
      if (srcTime>dstTime || dstTime==0)
      {
        // link is newer than the destination - it needs updating        
        textures.AddUnique(TextureInfo(filename,srcTime));
      }
    }
    
  }

  return true;
}


static AutoArray<RString> EmptyArray;

//! Functor for processing tex materials
struct TexMaterialProcess
{
  QOFStream &_dep;
  FindArrayKey<TextureInfo> &_textures;
  explicit TexMaterialProcess(FindArrayKey<TextureInfo> &textures, QOFStream &dep):_textures(textures),_dep(dep) {}
  bool operator () (const TexMaterial *mat, const TexMaterialBank::ContainerType *bank) const
  {
    if (!mat) return false;
    if (mat->GetName()[0]==0) return false;
    if (mat->GetName()[0]=='#') return false;
    _textures.AddUnique(TextureInfo(mat->GetName(),mat->GetTimestamp()));
    _dep << mat->GetName() << "\n";

    // the material may link to a surface info    
    const SurfaceInfo *src = mat->GetSurfaceInfo();
    if (src && src->_entryName.GetLength()>0 && src->_entryName[0]!='#')
    {
#if SINFO_RELOAD
      _textures.AddUnique(TextureInfo(src->_entryName,src->_timestamp));
#else
      _textures.AddUnique(TextureInfo(src->_entryName,0));
#endif
      _dep << src->_entryName << "\n";
    }
    return false;
  }
};

static void TransferTextures(
  FindArrayKey<TextureInfo> &textures, const char *fulltgtname,
  const AutoArray<RString> &moreSources=EmptyArray,
  const AutoArray<RString> &failedSources=EmptyArray
)
{
  // save dependency file
  BString<512> depName;
  strcpy(depName,fulltgtname);
  strcat(depName,".dep");
  // dependencies are of two kinds:
  // some need to be updated, some require the model to be updated as well
  QOFStream dep;
  dep.open(depName);

  for (int i=0; i<moreSources.Size(); i++)
  {
    dep << "+" << moreSources[i] << "\n";
  }
  for (int i=0; i<failedSources.Size(); i++)
  {
    dep << "-" << failedSources[i] << "\n";
  }
  
  // remember all textures currently in use
  AbstractTextBank *bank = GEngine->TextBank();
  LLinkArray<Texture> list;
  bank->GetTextureList(list);
  for (int i=0; i<list.Size(); i++)
  {
    Texture *tex = list[i];
    if (!tex) continue;
    if (tex->GetName().GetLength()<=0) continue;
    if (tex->GetName()[0]=='#') continue;
    textures.AddUnique(TextureInfo(tex->GetName(),tex->GetTimestamp()));
    dep << tex->GetName() << "\n";
  }
  
  // process materials as well
  GTexMaterialBank.ForEach(TexMaterialProcess(textures, dep));

  // process surface info as well
  for (int i=0; i<bank->NSurfaceInfos(); i++)
  {
    const SurfaceInfo *src = bank->GetSurfaceInfo(i);
    if (!src) continue;
    if (src->_entryName.GetLength()<=0) continue;
    if (src->_entryName[0]=='#') continue;
#if SINFO_RELOAD
    textures.AddUnique(TextureInfo(src->_entryName,src->_timestamp));
#else
    textures.AddUnique(TextureInfo(src->_entryName,0));
#endif
    dep << src->_entryName << "\n";
  }
}

#include "Es/Strings/bString.hpp"

static bool MatchMask(const char *mask, const char *string)
{
  if (!strcmp(mask,"*")) return true;           // single "*" matches all
  else if (!strcmpi(mask,string)) return true;  // exact match

  const char *wild = strchr(mask,'*');
  if (!wild) return false;                      // no wild card, no exact match => false
  
  int maskLen = strlen(mask);
  if (*mask=='*')
  { //wildcard at the beginning
    if (mask[maskLen-1]=='*')
    { //both start and end is '*'
      BString<2048> lowString(string); lowString.StrLwr();
      BString<512> lowMask(mask+1); lowMask.StrLwr(); lowMask[maskLen-2]=0; //clear leading and terminating '*'
      return strstr(lowString, lowMask)!=NULL;
    }
    else
    { //starts '*', rest must be exact match
      int strLen = strlen(string);
      return ( strLen>=(maskLen-1) && !stricmp(mask+1,string+strLen-maskLen+1) );  //*.p3d  xxx.p3d
    }
  }
  else if (mask[maskLen-1]=='*')
  { //only ends by '*'
    return (!strnicmp(mask,string,wild-mask));
  }
  //wildcard at the wrong place! Ignore...
  return false; 
}

bool CheckExclusion
(
  const FindArray<RString> &excludeFiles, const char *name, const char *fullname
)
{
  for(int i=0; i<excludeFiles.Size(); i++)
  {
    if
    (
      MatchMask(excludeFiles[i],name) ||
      MatchMask(excludeFiles[i],fullname)
    )
    {
      //RptF("Matched: mask='%s' [%s] full=%s", cc_cast(excludeFiles[i]), name, fullname);
      return true;
    }
  }
  return false;
}

struct BinarizeFolderContext
{
  FindArray<RString> excludeFiles;
  RString wild;
  /// textures and other files referenced from the model/world
  FindArrayKey<TextureInfo> textures;
  bool transferTextures;
  bool silent;
  bool makeAlways;
  RString texturePath;
  bool formatFP1;
  bool noSubdirs;
  ModelTarget _mt;
  // skeleton to use when no model config entry is given
  RString defaultSkeleton;
  // skeleton forced by the command line parameter
  RString skeleton;
  bool createTexHeaders;
};

static void OutputConfig(RString indent, ParamEntryVal cls)
{
  
  for (int i=0; i<cls.GetEntryCount(); i++)
  {
    ParamEntryVal subcls = cls.GetEntry(i);
    RptF("%s%s", (const char *)indent, (const char *)subcls.GetName());
    if (subcls.IsClass())
    {
      const ParamClass *subclsAsClass = subcls.GetClassInterface();
      if (subclsAsClass)
      {
        if (subclsAsClass->IsDefined())
        {
          OutputConfig(indent + RString("  "), subcls);
        }
        else if (subclsAsClass->IsDelete())
        {
          RptF("%sdelete %s",(const char *)indent,(const char *)subcls.GetName());
        }
        else
        {
          RptF("class %s;",(const char *)indent,(const char *)subcls.GetName());
        }
      }
    }
  }
}


#include <Es/ErrorProp/errorProp.hpp>

/*!
\patch_internal 1.52 Date 4/23/2002 by Jirka
- Fixed: check for config.cpp (or config.bin) when binarize addon to parse
*/

/*
static void LoadConfig(RString dir)
{
  RString filename = dir + RString("\\config.cpp");
  if (QFBankQueryFunctions::FileExists(filename))
  {
    ParamFile addon;

    ERROR_TRY()
      LSError error = addon.Parse(filename);
      if (error != LSOK)
      {
        RptF("Error %d while parsing",error);
        ERROR_THROW_NOVAL(ErrorInfo)
      }
    ERROR_CATCH_ANY(exc)
    {
      RptF("Error in config %s", (const char *)filename);
      RptF("Parsed entries:");
      OutputConfig("  ", addon);
      MessageBox(NULL, Format("Error in config %s", (const char *)filename), "Binarize", MB_OK + MB_ICONEXCLAMATION);
    }
    ERROR_END()

    Pars.Update(addon);
    Pars.SetFile(&Pars);
  }
  else
  {
    filename = dir + RString("\\config.bin");
    if (QFBankQueryFunctions::FileExists(filename))
    {
      ParamFile addon;
      addon.ParseBin(filename);
      Pars.Update(addon);
      Pars.SetFile(&Pars);
    }
  }
}
*/

static bool ParseConfig(const FileItem &file, BinarizeFolderContext &ctx)
{
  if (stricmp(file.filename, "config.cpp") == 0)
  {
    RString filename = file.path + file.filename;
    ParamFile addon;
    bool once = true;
    ERROR_TRY()
    LSError error = addon.Parse(filename, NULL, NULL, &GDefaultNamespace); // use default namespace
    if (error != LSOK)
    {
      RptF("Error %d while parsing",error);
      ERROR_THROW_NOVAL(ErrorInfo)
    }
    ERROR_CATCH_ANY(exc)
    {
      if (once)
      {
        once = false;
        RptF("Error in config %s", (const char *)filename);
        /*
        RptF("Parsed entries:");
        OutputConfig("  ", addon);
        */
        if (!ctx.silent)
        {
          MessageBox(NULL, Format("Error in config %s", (const char *)filename), "Binarize", MB_OK + MB_ICONEXCLAMATION);
        }
        fflush(stderr);
        _exit(EXIT_FAILURE); //return error code and finish (see https://projects.bistudio.com/dayz/Jenkins_setup_and_configuration-failonerror=_true_.txt)
      }
    }
    ERROR_END()

    Pars.Update(addon);
    Pars.SetFile(&Pars);
  }
  else if (stricmp(file.filename, "config.bin") == 0)
  {
    if (!QFBankQueryFunctions::FileExists(file.path + RString("config.cpp"))) // config.cpp has precedence
    {
      RString filename = file.path + file.filename;
      ParamFile addon;

      addon.ParseBin(filename);
      Pars.Update(addon);
      Pars.SetFile(&Pars);
    }
  }
  return false; // continue with enumeration
}

static void LoadConfig(BinarizeFolderContext &ctx, RString dir)
{
  ForEachFileR(dir + RString("\\"), ParseConfig, ctx);
};

/// while this class exists, any file being opened is reported
/**
Most functions only call corresponding _oldFunctions.
*/

template <class Callback>
class TrackFiles: public QIFileServerFunctions, private NoCopy
{
  QIFileServerFunctions *_oldFunctions;
  Callback &_callback;
  
  public:
  TrackFiles(Callback &callback)
  :_callback(callback)
  {
    _oldFunctions = GFileServerFunctions;
    GFileServerFunctions = this;
  }
  ~TrackFiles()
  {
    GFileServerFunctions = _oldFunctions;
  }

  //@{ implement QIFileServerFunctions using _oldFunctions
  virtual bool FlushReadHandle(const char *name)
  {
    return _oldFunctions->FlushReadHandle(name);
  }
  virtual bool Preload(
    FileRequestCompletionHandle &request, RequestableObject *obj, RequestableObject::RequestContext *ctx,
    FileRequestPriority priority, FileServerHandle handle, QFileSize start, QFileSize size
  )
  {
    return _oldFunctions->Preload(request,obj,ctx,priority,handle,start,size);
  }
  virtual PosQIStreamBuffer Read(FileServerHandle handle, QFileSize start, QFileSize size)
  {
    return _oldFunctions->Read(handle,start,size);
  }

  virtual PosQIStreamBuffer RequestResultAsync(FileRequestCompletionHandle &request, FileServerHandle handle, QFileSize pos)
  {
    return _oldFunctions->RequestResultAsync(request,handle,pos);
  }
  virtual InFileLocation RequestLocationAsync(const FileRequestCompletionHandle &request)
  {
    return _oldFunctions->RequestLocationAsync(request);
  }

  /// OpenReadHandle intercepted
  virtual FileServerHandle OpenReadHandle(const char *name)
  {
    FileServerHandle handle = _oldFunctions->OpenReadHandle(name);
    _callback(name,_oldFunctions->CheckReadHandle(handle));
    return handle;
  }
  virtual RString GetHandleName(FileServerHandle handle) const
  {
    return _oldFunctions->GetHandleName(handle);
  }
  virtual void CloseReadHandle(FileServerHandle handle)
  {
    return _oldFunctions->CloseReadHandle(handle);
  }
  virtual bool CheckReadHandle(FileServerHandle handle) const
  {
    return _oldFunctions->CheckReadHandle(handle);
  }
  virtual void MakeReadHandlePermanent(FileServerHandle handle)
  {
    return _oldFunctions->MakeReadHandlePermanent(handle);
  }

  virtual int GetPageSize()
  {
    return _oldFunctions->GetPageSize();
  }
  virtual void *GetWinHandle(FileServerHandle handle) const
  {
    return _oldFunctions->GetWinHandle(handle);
  }
  virtual QFileSize GetFileSize(FileServerHandle handle) const
  {
    return _oldFunctions->GetFileSize(handle);
  }
  virtual QFileTime TimeStamp(FileServerHandle handle) const
  {
    return _oldFunctions->TimeStamp(handle);
  }

  virtual Future<QFileTime> RequestTimeStamp(const char *name)
  {
    // is not very robust: _oldFunctions does not call our OpenReadHandle?
    // we do not care much: binarize does not use this call for timestamps
    Fail("Needs fixing");
    return _oldFunctions->RequestTimeStamp(name);
  }

  virtual bool OtherThreadsCanUse() const
  {
    // the callback is not thread safe
    return false;
  }
};

/// handler tracking any file being open
class OnOpenFile
{
  FindArrayRStringCI _filenamesOpened;
  FindArrayRStringCI _filenamesFailed;
  
  public:
  void operator () (const char *filename, bool success)
  {
    if (success)
    {
      _filenamesOpened.AddUnique(filename);
    }
    else
    {
      _filenamesFailed.AddUnique(filename);
    }
  }
  
  const AutoArray<RString> &Files() const {return _filenamesOpened;}
  const AutoArray<RString> &FilesFailed() const {return _filenamesFailed;}
  
};

struct UpdatePair
{
  RString fullScrName;
  RString fullTgtName;
  
  UpdatePair(RString src, RString tgt)
  :fullScrName(src),fullTgtName(tgt)
  {
  }
};
TypeIsMovableZeroed(UpdatePair)

void BinarizeFolder(
  BinarizeFolderContext &ctx,
  const char *sdir, const char *tdir
)
{
  // prepare paths
  char wildname[1024];
  char wilddirname[1024];
  char tgtname[1024];
  char srcname[1024];
  strcpy(wildname,sdir);
  strcpy(srcname,sdir);
  strcpy(tgtname,tdir);
  TerminateBy(wildname,'\\');
  TerminateBy(tgtname,'\\');
  strcpy(wilddirname,wildname);
  // we want all directories, ignoring the wildcard
  strcat(wilddirname,"*");
  // we want only files corresponding to the wildcard
  strcat(wildname,ctx.wild);

  TerminateBy(srcname,'\\');
  
  bool typesPreloaded = false;

  
  // traverse directories first
  _finddata_t fd; 
  long fh = _findfirst(wilddirname,&fd);
  if (fh!=-1)
  {
    // we perform a scan first - this helps disc cache
    //AutoArray<UpdatePair> needsUpdating;
    
    do
    {
      // check file
      //LogF("Check %s",fd.name);
      if (fd.attrib&_A_SUBDIR)
      {
        // Omit . and .. folders
        if(!strcmpi(fd.name,".") || !strcmpi(fd.name,"..")) continue;
        if (ctx.noSubdirs) continue;
        
        // Omit folders to be excluded
        {
          // check exclusion
          char lowname[256];
          strcpy(lowname, fd.name);
          strlwr(lowname);

          // check extension
          char fullsrcname[1024];
          strcpy(fullsrcname, srcname);
          strcat(fullsrcname, fd.name);

          // Check the folder
          if (CheckExclusion(ctx.excludeFiles, lowname, fullsrcname)) continue;
        }

        char sdirsub[1024];
        char tdirsub[1024];
        strcpy(sdirsub,sdir);
        strcpy(tdirsub,tdir);
        TerminateBy(sdirsub,'\\');
        TerminateBy(tdirsub,'\\');
        strcat(sdirsub,fd.name);
        strcat(tdirsub,fd.name);
        BinarizeFolder(ctx,sdirsub,tdirsub);
        continue; // ignore subdirs
      }
      // files have a separate pass
    } while (_findnext(fh,&fd)==0);
    _findclose(fh);
  }

  // scan files in the directory now
  fh = _findfirst(wildname,&fd);
  if (fh!=-1)
  {
    // we perform a scan first - this helps disc cache
    AutoArray<UpdatePair> needsUpdating;
    
    do
    {
      // check file
      //LogF("Check %s",fd.name);
      if (fd.attrib&_A_SUBDIR)
      {
        continue;
      }
      // check exclusion
      char lowname[256];
      strcpy(lowname,fd.name);
      strlwr(lowname);

      // check extension
      char fullsrcname[1024];
      char fulltgtname[1024];
      strcpy(fullsrcname,srcname);
      strcat(fullsrcname,fd.name);
      strcpy(fulltgtname,tgtname);
      strcat(fulltgtname,fd.name);
      //printf("%s -> %s\n", fullsrcname, fulltgtname);

      if (GShrinkP3D)
      {
        const char *ext = GetFileExt(fullsrcname);
        if (strcmpi(ext,".p3d")) continue;
      }

      bool exclude = CheckExclusion(ctx.excludeFiles,lowname,fullsrcname);
      if (exclude)
      {
        continue;
      }

      time_t srcTime = fd.time_write;
      // check target
      // do not convert if target is up to date
      if (
        !ctx.makeAlways &&
        CheckUpToDateWithDep(ctx.textures,srcTime,fulltgtname,ctx.texturePath)
      )
      {
        continue;
      }

      needsUpdating.Add(UpdatePair(fullsrcname,fulltgtname));
      
    } while (_findnext(fh,&fd)==0);
    _findclose(fh);



    for (int i=0; i<needsUpdating.Size(); i++)
    {
      const UpdatePair &pair = needsUpdating[i];
      const char *ext = GetFileExt(pair.fullScrName);
      if (!strcmpi(ext,".p3d"))
      {
        printf(
          "Convert model %s -> %s\n",
          cc_cast(pair.fullScrName), cc_cast(pair.fullTgtName)
         );
        fprintf(stderr, "<model = \"%s\">\n", cc_cast(pair.fullScrName));
        // convert p3d object
        Ref<LODShape> shape;
        // loaded correctly
        bool ok = false;
        // list of files opened during target processing
        OnOpenFile onOpenFile;
        {
          TrackFiles<OnOpenFile> trackFiles(onOpenFile);
          shape = new LODShape();
          ok = shape->Load(pair.fullScrName, false, ctx._mt);
        }
        if (ok)
        {
          MakeAllDirs(pair.fullTgtName);
          shape->SaveOptimized(pair.fullTgtName);
          // remember all textures currently in use
          if (ctx.transferTextures)
          {
            // all configs we depend on need to be linked
            TransferTextures(
              ctx.textures,pair.fullTgtName,
              onOpenFile.Files(),onOpenFile.FilesFailed()
              );
          }
        }
        else
        {
          fprintf(stderr, "Loading of model %s failed\n", cc_cast(pair.fullScrName));
        }
        fprintf(stderr, "</model>\n");
      }
      else if (!strcmpi(ext,".wrp"))
      {
        if (!typesPreloaded)
        {
          //VehicleTypes.Preload();
          typesPreloaded = true;
        }
        printf(
          "Convert world %s -> %s\n",
          cc_cast(pair.fullScrName), cc_cast(pair.fullTgtName)
        );
        fprintf(stderr, "<world = \"%s\">\n", cc_cast(pair.fullScrName));
        SRef<Landscape> land = new Landscape(GEngine,GWorld,true);
        GLandscape = land;
        char islandName[256];
        GetFilename(islandName,pair.fullScrName);
        ParamEntryVal worlds = Pars >> "CfgWorlds";
        ConstParamEntryPtr clsE =  worlds.FindEntry(islandName);
        ParamEntryVal cls = clsE ? *clsE : worlds >> "DefaultWorld";
        land->LoadData(pair.fullScrName, cls, ctx.texturePath);
        land->ResetGeography();
        land->InitGeography();
        MakeAllDirs(pair.fullTgtName);
        if (!ctx.formatFP1)
          land->SaveOptimized(pair.fullTgtName);
        else
          land->SaveOptimizedFP1(pair.fullTgtName);

        if (ctx.transferTextures) TransferTextures(ctx.textures,pair.fullTgtName);
        
        fprintf(stderr, "</world>\n");
      }
      else if (!strcmpi(ext,".rtm"))
      {
        // process rtm 

        printf(
          "Convert animation %s -> %s\n",
          cc_cast(pair.fullScrName), cc_cast(pair.fullTgtName)
         );
        fprintf(stderr, "<anim = \"%s\">\n", cc_cast(pair.fullScrName));
        // note: we need to know the skeleton for the rtm
        // we can use auto-detection based on bone names?
        // currently we are most interested in the human skeleton
        // human skeleton is defined in model.cfg/CfgSkeletons/OFP2_ManSkeleton
        // we load this skeleton and verify that it matches the rtm
        // TODO: traverse all skeletons in CfgSkeletons and use the best one
        // list of files opened during target processing
        OnOpenFile onOpenFile;
        {
          QIFStreamB in;
          in.AutoOpen(pair.fullScrName);
          if (!in.fail() && !in.eof())
          {
            MakeAllDirs(pair.fullTgtName);
            
            TrackFiles<OnOpenFile> trackFiles(onOpenFile);
            
            // parse the model config, and load a skeleton
            
            //  open the model config
            ModelConfig modelCfg(pair.fullScrName);

            //  decide the skeleton name
            RString skeletonName = ctx.skeleton;
            if (skeletonName.IsEmpty())
            {
              // none enforced by the command line
              ConstParamEntryPtr ptr = modelCfg.GetCfgSkeletons().FindEntry("skeleton");
              if (ptr) skeletonName = *ptr;
              if (skeletonName.IsEmpty())
              {
                // use the default
                skeletonName = ctx.defaultSkeleton;
              }
            }

            SkeletonSourceName ssName(modelCfg.GetCfgSkeletons(), skeletonName);
            Ref<SkeletonSource> source = SkeletonSources.New(ssName);
            Ref<Skeleton> skeleton = new Skeleton(skeletonName);
            bool matching = false;
            if (skeleton && AnimationRT::Match(in,skeleton,pair.fullScrName)>0)
            {
              matching = true;
      
              // we do not want to report any missing bones - during binarization it is normal
              skeleton->SetReportMissingBones(false);
              // we know the skeleton - now load the animation
              AnimationRTName name;
              name.name = pair.fullScrName;
              name.skeleton = skeleton;
              // all human animations and models are currently reversed
              name.reversed = true;
              Ref<AnimationRT> anim = new AnimationRT(name);
              // now we have the anim - time to save it
              anim->SaveOptimized(skeleton,pair.fullTgtName);
              // no textures need to be transferred, however we like to record our dependencies
              if (ctx.transferTextures)
              {
                // all configs we depend on need to be linked
                TransferTextures(
                  ctx.textures,pair.fullTgtName,
                  onOpenFile.Files(),onOpenFile.FilesFailed()
                );
              }
            }
            else
            {
              // no match - simply copy the file
              QOFStream out;
              out.open(pair.fullTgtName);
              in.seekg(0, QIOS::beg);
              in.copy(out);
            }

            fprintf(stderr, "  <skeleton name=\"%s\" path=\"%s\" matching=\"%s\">\n", cc_cast(ssName._name), cc_cast(modelCfg.GetName()), matching ? "true" : "false");
          }
        }
        
        fprintf(stderr, "</anim>\n");
      }
      else if (!strcmpi(ext,".lst"))
      {
        char fullfilename[1024];
        strcpy(fullfilename,srcname);
        strcat(fullfilename,"textures.lst");

        if (!strcmpi(pair.fullScrName,fullfilename) != NULL) 
        {//matches textures.lst in srcname dir (called recursively for all dirs in processed dir tree)
          printf("Found %s, inserting textures not linked by .p3d-s...\n",fullfilename);
          if (ctx.transferTextures)
          {
            ProcessNotLinkedTextures(ctx.textures, fullfilename,ctx.texturePath);
            TransferTextures(ctx.textures,pair.fullTgtName);
          };
        }
      }

      Shapes.ClearCache();
      GTexMaterialBank.ClearCache();
      GEngine->TextBank()->ClearTexCache();
      //GMultipassMaterialBank.ClearCache();
      // no textures should be loaded now - all caches should be flushed
    }

  }
}



/// used as an implementation for Shape
static int MakeSourceFile(const char *name, const char *dest)
{
  // we need to release any handles to the target file
  GFileServerFunctions->FlushReadHandle(dest);

  // If source file is in progress, then wait for it to be finished
  FCProcessHandles *item = GFCPHManager.Find(RStringB(name));
  if (item) GFCPHManager.FinishProcessAndDeleteItem(item);

  // we need to crate a target directory
  // TODO: move to GFileServerFunctions or similar place
	void CreatePath(RString path);
	CreatePath(dest);

  if (GEngine->IsAsynchronousTextureLoadingSupported())
  {
    // Create new process
    BinMakeHandle processHandle;
    if (GFileMakeFunctions->Make(name,dest,"binarize",&processHandle) >= 0)
    {
      GFCPHManager.AddItem(dest, (HANDLE)processHandle);
      return 0;
    }
    return -1;
  }
  else
  {
    return GFileMakeFunctions->Make(name,dest,"binarize",NULL);
  }
}

/**
\param dir directory
\param rel relative path from the root directory of the search
*/
static void AddDirToTHDManager(const BinarizeFolderContext& ctx, TextureHeaderManager& mng, const RString& sourceDir, const RString& relPath)
{
  RString dir = sourceDir;
  NormalizePathRString(dir);
  RString rel = relPath;
  NormalizePathRString(rel);

  static const RString DIR_STR("\\");
  _finddata_t fInfo;
  RString wild = dir + DIR_STR + RString("*");
  long hFile=_findfirst(wild,&fInfo);
  if( hFile!=-1 )
  {
    do
    {
      RString fileName = fInfo.name;
      fileName.Lower();
      RString subrel = ((rel.IsEmpty()) ? fileName : (rel+DIR_STR+fileName));

      NormalizePathRString(subrel);

      // check exclusion
      if (CheckExclusion(ctx.excludeFiles, fileName, subrel))
        continue;

      if( fInfo.attrib&_A_SUBDIR )
      {
        // subdirectory

        if (ctx.noSubdirs)
          continue;
      
        if( fInfo.name[0]=='.' ) continue;
        RString subdir=dir+DIR_STR+fileName;
        AddDirToTHDManager(ctx, mng, subdir,subrel);
        continue;
      }

      //check extension
      char *ext = GetFileExt(fInfo.name);
      if (!ext || *ext == 0)
        continue; //no extension

      if (stricmp(ext, ".paa") != 0 && stricmp(ext, ".pac") != 0)
        continue; //not a texture

      // add to manager
      RString name = dir+DIR_STR+fileName;
      if (!mng.AddHeaderFromFile(name, subrel))
      {
        printf("Failed adding texture %s to texture header manager.\n", cc_cast(name));
      }
    }
    while( _findnext(hFile,&fInfo)==0 );
    _findclose( hFile );
  }     
}

/// creates texture header file for all textures in given sourceDir, result file is placed in destDir
static int CreateTextureHeadersFile(const BinarizeFolderContext& ctx, const char* sourceDir, const char *destDir)
{
  printf("Creating texture headers file...\n");
  TextureHeaderManager mng;

  RString destinationDir;
  // use destination from -textures parameter, if present
  if (ctx.transferTextures)
  {
    destinationDir = ctx.texturePath;
  }
  else
  {
    destinationDir = destDir;
  }
  if (destinationDir[destinationDir.GetLength()-1]!='\\') destinationDir = destinationDir+ "\\";

  AddDirToTHDManager(ctx, mng, sourceDir, RString());
  if (mng.GetCount())
  {
    RString name = destinationDir + TextureHeaderManager::DefaultFileName;
    if (mng.SaveToFile(name))
    {
      printf("%d texture headers saved to file \"%s\"\n", mng.GetCount(), cc_cast(name));
      return 0;
    }
    {
      printf("Error: saving texture headers to file \"%s\" failed.\n", cc_cast(name));
      return -1;
    }
  }
  else
  {
    printf("Texture headers file was not created - no textures found.\n");
    return 0;
  }
}

Engine *CreateEngineDummy();
void InitModules();
void PosAccessInit(const char *folder)
{
  static bool once = true;
  if (!once) return;
  once = false;


  //GFileServer=new FileServerST(10*1024*1024);

  GFileServer->Start();

  // CHANGE AFTER RELEASE 1.00 (INCLUDE STRUCTURE CHANGED IN config.cpp)
/*
  char buf[1024];
  GetCurrentDirectory(sizeof(buf),buf);
  SetCurrentDirectory(RString(folder) + RString("bin"));
  Pars.Parse("config.cpp");

  GLanguage = "English";
  LoadStringtable("Global", "stringtable.csv", 0, false);

  SetCurrentDirectory(buf);
*/
  RString dir = RString(folder) + RString("bin\\");
  Pars.Parse(dir + RString("config.cpp"), NULL, NULL, &GDefaultNamespace); // use default namespace
  GLanguage = "English";
  LoadStringtable("Global", dir + RString("stringtable.csv"), 0, false);
  
  InitModules();

  //GGameState.Init();

}

void PosAccessCreateEngine()
{
  GEngine = CreateEngineDummy();
  if (GEngine)
  {
    GEngine->Init();
  }
}


void PosAccessDone()
{
  Pars.Clear();
  Pars.DeleteVariables();
}


static void Usage()
{
  printf
  (
    "Usage:\n"
    "binarize [options] source destination [wildcard]\n\n"
    "Options:\n"
    "\t-textures=<folder> copy all used textures to folder\n"
    "\t-exclude=<file> file contains list of files to exclude\n"
    "\t-norecurse do not traverse any subdirectories\n"
    "\t-addon=<addon folder> config is loaded from this folder\n"
    "\t-binpath=<folder> placement of bin directory with the main config\n"
    "\t-FP1 output in format for FP1 (working just on *.wrp)\n"
    "\t-always binarize even if not necessary based on timestamps\n"
    "\t-silent never display any message box, never wait for any user input\n"
    "\t-maxProcesses=<n> allow spawning max. n child processes for conversions\n"
    "\t-targetBonesInterval=<n> how many bones are allowed in one section when binarizing p3d file (default: 55)\n"
    "\t-skeleton=<name> which skeleton should be used for rtm binarization (default: OFP2_ManSkeleton)\n"
    "\t-texheader only creates texture headers from source dir, file is placed in destination dir\n"
    "\t-shrinkP3D reduce models LODs. Affects only *.p3d files\n"
    "\n"
    "\nCopyright (c) 2002-2007 Bohemia Interactive Studio.\n"
    "All rights reserved.\n"
  );
}

/*!
\patch_internal 1.52 Date 4/23/2002 by Jirka
- Changed: search for global config in "X:\binarize\bin"
*/

int main(int argc, char* argv[])
{
  // redirect AppFrameFunctions to console stdout,stderr
  CurrentAppFrameFunctions = &GBinarizeFrameFunctions;

	GetNetworkManager().DisablePhysics();

  if (argc<2)
  {
    Usage();
		GetNetworkManager().Done(); 
    GetNetworkManager().DisableDone(); //disable other calls of Done() in destructor
    return 1;
  }

  // BuldozerBinMakeFunctions uses #ifndef _CONSOLE, we want to disable BinMakeConsoleThread
  void DisableBinMakeConsoleThread();
  DisableBinMakeConsoleThread();

  //__asm int 3;
  
  RString binPath = "binarize\\";
  
  // look for "-binpath=" argument (must be before PosAccessInit)
  for (int i=1; i<argc; i++)
  {
    const char *rest;
    if ((rest = RestArg(argv[i], "-binpath=")) != NULL)
    {
      // get rest of argument
      char tp[512];
      strcpy(tp, rest);
      TerminateBy(tp, '\\');
      binPath = tp;
    }
  }

  bool texHeaderMode = false;
  // look for "-texheader" argument
  for (int i=1; i<argc; i++)
  {
    const char *rest;
    if ((rest = RestArg(argv[i], "-texheader")) != NULL)
    {
      texHeaderMode = true;
      break;
    }
  }


  void SetBinMakeFunctions();
  SetBinMakeFunctions();

  GFileServer->SetMaxSize(256*1024*1024);
  
  if (!texHeaderMode)
  {
    PosAccessInit(binPath);
    VehicleTypes.Init();
  }
  BinarizeFolderContext ctx;
  ctx.transferTextures = false;
  ctx.silent = false;
  ctx.makeAlways = false;
  ctx.formatFP1 = false;
  ctx.defaultSkeleton = "OFP2_ManSkeleton";
  ctx.skeleton = "";
  ctx.noSubdirs = false;
  ctx.createTexHeaders = texHeaderMode;
  //char texturePath[512];
  //char addonPath[512];
  // scan directory, load all files and save them in binary format
  while (argc>1 && *argv[1]=='-')
  {
    // detect some special arguments
    // process argument
    const char *rest;
    if ((rest = RestArg(argv[1],"-textures="))!=NULL)
    {
      // get rest of argument
      char tp[512];
      strcpy(tp,rest);
      TerminateBy(tp,'\\');
      ctx.texturePath = tp;
      ctx.transferTextures = true;
    }
    else if ((rest = RestArg(argv[1],"-server"))!=NULL)
    {
      ctx.silent = true;
    }
    else if ((rest = RestArg(argv[1],"-silent"))!=NULL)
    {
      RptF("Binarize run in silent mode!");
      ctx.silent = true;
    }
    else if ((rest = RestArg(argv[1],"-norecurse"))!=NULL)
    {
      ctx.noSubdirs = true;
    }
    else if ((rest = RestArg(argv[1],"-addon="))!=NULL)
    {
      LoadConfig(ctx, rest);
      //ctx.addonPath = rest;
    }
    else if ((rest = RestArg(argv[1],"-exclude="))!=NULL)
    {
      FILE *f = fopen(rest,"r");
      if (f)
      {
        // read single filename
        char line[1024];
        while (fgets(line,sizeof(line),f))
        {
          // remove end of line
          int l = strlen(line);
          if (l>0 && line[l-1]=='\n') line[l-1] = 0;
          if (*line==0 || *line==';') continue;
          strlwr(line);
          ctx.excludeFiles.Add(line);
        }
        fclose(f);
      }      
    }
    else if ((rest = RestArg(argv[1],"-shrinkP3D"))!=NULL)
    {
      GShrinkP3D = true;
    }
    else if ((rest = RestArg(argv[1],"-FP1"))!=NULL)
    {
      ctx.formatFP1 = true;
    }
    else if ((rest = RestArg(argv[1],"-always"))!=NULL)
    {
      ctx.makeAlways = true;
    }
    else if ((rest = RestArg(argv[1], "-binpath=")) != NULL)
    {
      // already processed
    }
    else if ((rest = RestArg(argv[1], "-targetBonesInterval=")) != NULL)
    {
      ctx._mt._bonesInterval = atoi(rest);
    }
    else if ((rest = RestArg(argv[1],"-maxProcesses="))!=NULL)
    {
      GFCPHManager.SetMaxProcesses(std::max(1, atoi(rest)));
    }
    else if ((rest = RestArg(argv[1],"-skeleton="))!=NULL)
    {
      ctx.skeleton = rest;
    }
    else if ((rest = RestArg(argv[1],"-texheader"))!=NULL)
    {
      ctx.createTexHeaders = true;
    }
    else 
    {
      printf("Unknown argument %s\n",argv[1]);
    }
    argv++;
    argc--;
  }

  LandEditor = false;

  if (argc>=3 && argc<=4)
  {
    // force finding sections
    const char *sdir = argv[1];
    const char *tdir = argv[2];
    ctx.wild = "*";
    if (argc>=4)
    {
      ctx.wild = argv[3];   
    }

    if (ctx.createTexHeaders)
    {
      // we are only creating texture headers
      CreateTextureHeadersFile(ctx, sdir, tdir);
      GFileServer->Clear();
      GetNetworkManager().Done(); 
      GetNetworkManager().DisableDone(); //disable other calls of Done() in destructor
      return 0;
    }

    // scan all files

    // load addon config (recursive)
    LoadConfig(ctx, sdir);

    PosAccessCreateEngine();
    GWorld=new World(GEngine,LandEditor,true);

    BinarizeFolder(ctx,sdir,tdir);

    if (ctx.transferTextures && ctx.textures.Size()>0)
    {
      // transfer all textures from to destination folder

      //int percentShown = -1;
      for (int i=0; i<ctx.textures.Size(); i++)
      {
        /*
        int percentDone = i*100/ctx.textures.Size();
        if (percentDone!=percentShown)
        {
          percentShown = percentDone;
          printf("\rChecking Textures: %3d %%",percentShown);
        }
        */
        RString name = ctx.textures[i].name;
        if (name[0]=='#')
        {
          // do not try to copy procedural textures or materials
          continue;
        }
        // do not copy if texture already exists
        int time = ctx.textures[i].time;
        RString dest = RString(ctx.texturePath) + name;
        if (CheckUpToDate(time,dest))
        {
          continue;
        }
        if (CheckExclusion(ctx.excludeFiles,name,name))
        {
          continue;
        }
        MakeSourceFile(name, dest);
      }


//      if (percentShown>=0)
//      {
//        printf("\rChecking Textures: %3d %\n",100);
//      }
    }

    ClearShapes();
    VehicleTypes.Clear();
    if( GEngine ) delete GEngine,GEngine = NULL;
    GetNetworkManager().Done(); 
    GetNetworkManager().DisableDone(); //disable other calls of Done() in destructor
    GFileServer->Clear();
    PosAccessDone();
    return 0;
  }
  Usage();
  ClearShapes();
  VehicleTypes.Clear();
  if( GEngine ) delete GEngine,GEngine = NULL;
  GetNetworkManager().Done(); 
  GetNetworkManager().DisableDone(); //disable other calls of Done() in destructor
  GFileServer->Clear();
  PosAccessDone();
  return 1;
}

// some cross imports from posAccess
#pragma comment(lib,"winmm")
#pragma comment(lib,"dxguid")
#pragma comment(lib,"dinput8")
#pragma comment(lib,"version")

#if _DEBUG
#pragma comment(lib,"dxerr")
#endif

#endif

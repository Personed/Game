// XBOXPBORepack.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <El/elementpch.hpp>
#include <ES/essencepch.hpp>
#include <Es/Containers/rStringArray.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/QStream/packFiles.hpp>
#include <Es/Algorithms/qsort.hpp>
#include <lib/pactext.hpp>
#include <io.h>
#include <conio.h>
#include "El/Stringtable/iExtParser.hpp"
#include "El/Stringtable/StringTable.hpp"
#include "StringtableXMLToCSVParser.h"
#include "StringtableXMLToBinParser.h"

const RString DIR_STR("\\");
const char DIR_CHR('\\');

DataStack GDataStack(1*1024*1024);

class PBORepack
{
public:
  // stringtable convert type
  enum STConvertType
  {
    STCNone, ///< do not convert
    STCCSV, ///< convert to CSV
    STCBIN ///< convert to BIN
  };

  PBORepack();

  /*!
  Packs pbo based on given log. Files in the PBO will be in the same order as in the log. Files that are not in the log, but are in the folder
  are added to the PBO after the files in the log.
  \param srcFolder path to the folder we want to pack
  \param outFileName name of the output PBO
  \param prefix prefix of the PBO
  \param logFileName name of the log. Each line in the log contains file name
  \param textureHeadersFile name of output file for texture headers (if empty, then is not created)
  \param stConvertType how we should convert stringtable.xml files
  */
  bool Pack(const RString& srcFolder, const RString& outFileName, const RString& logFileName, 
    const RString& pboPrefix, const RString& textureHeadersFile, STConvertType stConvertType);

  /// takes stringtable XML and saves the file to CSV
  static bool SaveStringtableXMLAsCSV(const RString& xmlFileName, const RString& csvFileName);
  /// takes stringtable XML and saves the file to binary file
  static bool SaveStringtableXMLAsBIN(const RString& xmlFileName, const RString& binFileName);
private:
  /// used to report messages to user
  void Report(char const *format,...)
  {
    va_list arglist;
    va_start(arglist, format);

    vprintf(format, arglist);

    va_end(arglist);
  }
  void ReportSep()
  {
    Report("-------------------------------------------\n");
  }

  bool CreateSrcFolderFileList();
  bool CreateLogFileList(const RString& logFileName);
  bool ScanDir(const RString& dir, const RString& rel = RString(), int level=0);
  bool CreateFinalList();
  bool CreateTextureHeadersFile(const RString& fileName);
  bool CreateStringtableCSVFiles();
  bool CreateStringtableBINFiles();
  void CleanUpTmpFiles();

  /// infos about all files in source folder
  MapStringToClass<FileInfoExt, AutoArray<FileInfoExt>> _fileInfos;
  /// file names from the log file
  FindArrayRStringCI _logFileNames;
  /// source folder
  RString _srcFolder;
  /// final list of files for packer
  AutoArray<FileInfoExt> _files;
  /// total size of all files
  __int64 _totalFileSize;
  /// prefix of the PBO
  RString _pboPrefix;
  /// how we should convert stringtable.xml files
  STConvertType _stConvertType;
  /// list of tmp files we created 
  AutoArray<RString> _tmpFiles;
};

PBORepack::PBORepack() : _totalFileSize(0), _stConvertType(STCNone)
{

}

bool PBORepack::SaveStringtableXMLAsCSV(const RString& xmlFileName, const RString& csvFileName)
{
  QIFStreamB in;
  if (!GStringtableExtParserFunctions->Open(xmlFileName, in))
    return false;

  StringtableXMLToCSVParser parser(csvFileName);
  return parser.Parse(in);
}

bool PBORepack::SaveStringtableXMLAsBIN(const RString& xmlFileName, const RString& binFileName)
{
  QIFStreamB in;
  if (!GStringtableExtParserFunctions->Open(xmlFileName, in))
    return false;

  StringtableXMLToBINParser parser(binFileName);
  return parser.Parse(in);
}

bool PBORepack::Pack(const RString& srcFolder, const RString& outFileName, const RString& logFileName,
                     const RString& pboPrefix, const RString& textureHeadersFile, STConvertType stConvertType)
{
  Report("Packing started.\n");
  Report("Source folder: %s\n", srcFolder.Data());
  Report("Target file: %s\n", outFileName.Data());
  Report("PBO prefix: %s\n", pboPrefix.Data());
  Report("Log file: %s\n", logFileName.Data());
  Report("Texture headers file: %s\n", textureHeadersFile.Data());
  Report("Convert stringtable.xml : %s\n", (stConvertType == STCNone) ? "none" : (stConvertType == STCBIN ? "bin" : "csv")) ;
  ReportSep();

  _stConvertType = stConvertType;
  _tmpFiles.Clear();
  _srcFolder = srcFolder;
  _pboPrefix = pboPrefix;
  if (!CreateSrcFolderFileList())
  {
    Report("Creation of list of files in source folder failed.\n");
    return false;
  }
  if (!CreateLogFileList(logFileName))
  {
    Report("Creation of list of files in log file failed.\n");
    return false;
  }
  if (!CreateFinalList())
  {
    Report("Creation of final list of files failed.\n");
    return false;
  }

  if (!outFileName.IsEmpty())
  {
    switch (_stConvertType)
    {
    case STCCSV:
      CreateStringtableCSVFiles();
      break;
    case STCBIN:
      CreateStringtableBINFiles();
      break;
    }

    Report("Packing...\n");
    QFProperty prop;
    prop.name = "prefix";
    prop.value = pboPrefix;
    FileBankManager packer;
    packer.Create(outFileName.Data(), srcFolder, false, true, _files, NULL, &prop, 1);

    CleanUpTmpFiles();
  }

  if (!textureHeadersFile.IsEmpty())
  {
    if (!CreateTextureHeadersFile(textureHeadersFile))
    {
      Report("Creation of texture headers file failed!.\n");
      return false;
    }
  }
  return true;
}

bool PBORepack::ScanDir(const RString& dir, const RString& rel, int level)
{
  _finddata_t fInfo;
  RString wild = dir + RString("\\*");
  long hFile=_findfirst(wild,&fInfo);
  if( hFile!=-1 )
  {
    do
    {
      if (stricmp(fInfo.name, "$PBOPREFIX$") == 0)
        continue;
      RString subrel = rel+RString(DIR_STR)+RString(fInfo.name);
      if( fInfo.attrib&_A_SUBDIR )
      {
        if( fInfo.name[0]=='.' ) continue;
        RString subdir=dir+RString(DIR_STR)+RString(fInfo.name);
        ScanDir(subdir,subrel, level+1);
        continue;
      }
      FileInfoExt f;
      const char *rc=subrel;
      if( *rc==DIR_CHR ) rc++;

      memset(&f,0,sizeof(f));
      f.name = rc;
      f.name.Lower();
      f.length=fInfo.size;
      f.time=fInfo.time_write;
      f.priority = level;
      f.compressedMagic = 0;
      f.uncompressedSize = 0;
      _totalFileSize += f.length;
      _fileInfos.Add(f);
      if (_fileInfos.NItems() % 100 == 0)
        Report(".");
    }
    while( _findnext(hFile,&fInfo)==0 );
    _findclose( hFile );
  }     

  return true;
}

bool PBORepack::CreateSrcFolderFileList()
{
  Report("Creating list of files in source folder");
  _fileInfos.Clear();
  _totalFileSize = 0;

  ScanDir(_srcFolder);

  if (_fileInfos.NItems() == 0)
  {
    Report("\nNo file found in given folder!\n", _fileInfos.NItems());
    return false;
  }  

  static const __int64 maxSize = 4LL*1024LL*1024LL*1024LL;
  if (_totalFileSize >=  maxSize)
  {
    Report("\nTotal size of all files is greater than 4GB, aborting.\n");
    return false;
  }

  Report("\nList of files in source folder created. Found %d files, total size %u\n", _fileInfos.NItems(), (unsigned int)_totalFileSize);
  return true;
}

bool PBORepack::CreateLogFileList(const RString& logFileName)
{
  if (logFileName.IsEmpty())
    return true;

  Report("Creating list of files in the log file.\n");

  _logFileNames.Clear();
  QIFStream file;
  file.open(logFileName.Data());
  if (file.fail())
  {
    Report("Failed to open log file \"%s\"!\n", logFileName.Data());
    return false;
  }

  char fname[1024];
  int count = 0;
  while (!file.fail() && !file.eof())
  {   
    file.readLine(fname, 1024);
    NormalizePath(fname);
    int length = strlen(fname);
    if (length < 3)
      continue;
    // we want only from ca
    if (strstr(fname, "ca\\") != fname)
      continue;
    char* newName = fname+3;   
    if (_logFileNames.AddUnique(newName) != -1)
      count++;
  }

  file.close();
  Report("Number of files read from the log: %d\n", count);
  return true;
}

static int CompareFileInfoPBORepack( const FileInfoExt *f1, const FileInfoExt *f2 )
{
  // priority is the level of subdirectory of the file (see ScanDir)
  int d = f1->priority - f2->priority;
  if (d) return d;
  return strcmpi(f1->name,f2->name);
}

bool PBORepack::CreateFinalList()
{
  Report("Creating list of files for packing.\n");
  _files.Clear();

  int logCount = 0;
  for (int i=0; i<_logFileNames.Size(); ++i)
  {
    if (_logFileNames[i].IsEmpty())
      continue;
    const FileInfoExt& info = _fileInfos.Get(_logFileNames[i]);
    if (_fileInfos.NotNull(info))
    {
      _files.Add(info);
      _fileInfos.Remove(_logFileNames[i]);
      logCount++;
    }
    else
    {
      Report("File \"%s\" from log not found in the source folder!\n", _logFileNames[i].Data());
    }
  }

  if (_fileInfos.NItems() > 0)
  {
    for (int i=0; i<_fileInfos.NTables(); i++)
    {
      AutoArray<FileInfoExt> &container = _fileInfos.GetTable(i);
      for (int j=0; j<container.Size(); j++)
      {
        const FileInfoExt &info = container[j];
        _files.Add(info);
      }
    }
  }

  // sort the infos, that are not from the log, by subdirectory level and alphabetically
  if (_files.Size() > 0 && _files.Size() > logCount)
  {
    QSort(_files.Data() + logCount, _files.Size() - logCount, CompareFileInfoPBORepack);
  }

  Report("Files from log: %d, remaining files: %d\n", logCount, _fileInfos.NItems());

  return true;
}

bool PBORepack::CreateTextureHeadersFile(const RString& fileName)
{
  Report("Creating texture headers file.\n");

  TextureHeaderManager mng;

  for (int i=0; i<_files.Size(); ++i)
  {
    const FileInfoExt& info = _files[i];
    if (info.name.IsEmpty())
      continue;

    // get extension
    int pos = info.name.ReverseFind('.');
    if (-1 == pos)
      continue;
    RString ext = info.name.Substring(pos+1, info.name.GetLength());
    ext.Lower();

    // we want only paa and pac files
    if (ext != RString("paa") && ext != RString("pac"))
      continue;

    RString name = _srcFolder + DIR_STR + info.name;
    RString texName = _pboPrefix + DIR_STR + info.name;

    mng.AddHeaderFromFile(name, texName);

    if (i % 100 == 0)
    {
      if (i==0)
        Report("%00d%%");
      else
        Report("\b\b\b%02d%%", (int)(i/(float)_files.Size()*100));
    }
  }

  Report("\b\b\b100%\n");
  // save manager
  return mng.SaveToFile(fileName);
}

bool PBORepack::CreateStringtableBINFiles()
{
  /// stringtable name
  const RString stName = "stringtable.xml";

  Report("Creating BIN files from stringtable XML files.");
  _tmpFiles.Clear();

  // number of files created
  int count = 0;
  for (int i=0; i<_files.Size(); ++i)
  {
    FileInfoExt& info = _files[i];
    if (info.name.IsEmpty())
      continue;

    if (i % 100 == 0)
      Report(".");


    // check if file is stringtable.xml
    if (info.name.GetLength() < stName.GetLength())
      continue;

    int offset = info.name.GetLength() - stName.GetLength();
    if (stricmp(stName.Data(), info.name.Data() + offset) != 0)
      continue;

    // create csv name
    RString binName = info.name.Substring(0, info.name.GetLength()-3) + "bin";

    // full path
    RString xmlFullPath = _srcFolder + DIR_STR + info.name;
    RString binFullPath = _srcFolder + DIR_STR + binName;

    if (QIFileFunctions::FileExists(binFullPath.Data()))
      continue; //already exists

    if (!SaveStringtableXMLAsBIN(xmlFullPath, binFullPath))
    {
      Report("Creating of BIN file \"%s\" failed!\n", binFullPath.Data());
      continue;
    }

    _tmpFiles.Add(binFullPath);

    // now we must replace file info for this item
    info.name = binName;
    info.name.Lower();
    info.compressedMagic = 0;
    info.uncompressedSize = 0;

    _totalFileSize -= info.length;
    info.length= QIFileFunctions::GetFileSize(binFullPath.Data());
    _totalFileSize += info.length;

    info.time = QIFileFunctions::TimeStamp(binFullPath.Data());
    // info.priority stays the same


    count++;
  } 

  Report("\n%d BIN files created.\n", count);
  return true;
}

bool PBORepack::CreateStringtableCSVFiles()
{
  /// stringtable name
  const RString stName = "stringtable.xml";

  Report("Creating CSV files from stringtable XML files.");
  _tmpFiles.Clear();

  // number of files created
  int count = 0;
  for (int i=0; i<_files.Size(); ++i)
  {
    FileInfoExt& info = _files[i];
    if (info.name.IsEmpty())
      continue;

    if (i % 100 == 0)
      Report(".");

    
    // check if file is stringtable.xml
    if (info.name.GetLength() < stName.GetLength())
      continue;

    int offset = info.name.GetLength() - stName.GetLength();
    if (stricmp(stName.Data(), info.name.Data() + offset) != 0)
      continue;

    // create csv name
    RString csvName = info.name.Substring(0, info.name.GetLength()-3) + "csv";

    // full path
    RString xmlFullPath = _srcFolder + DIR_STR + info.name;
    RString csvFullPath = _srcFolder + DIR_STR + csvName;

    if (QIFileFunctions::FileExists(csvFullPath.Data()))
      continue; //already exists

    if (!SaveStringtableXMLAsCSV(xmlFullPath, csvFullPath))
    {
      Report("Creating of CSV file \"%s\" failed!\n", csvFullPath.Data());
      continue;
    }
    
    _tmpFiles.Add(csvFullPath);

    // now we must replace file info for this item
    info.name = csvName;
    info.name.Lower();
    info.compressedMagic = 0;
    info.uncompressedSize = 0;

    _totalFileSize -= info.length;
    info.length= QIFileFunctions::GetFileSize(csvFullPath.Data());
    _totalFileSize += info.length;

    info.time = QIFileFunctions::TimeStamp(csvFullPath.Data());
    // info.priority stays the same


    count++;
  } 

  Report("\n%d CSV files created.\n", count);
  return true;
}

void PBORepack::CleanUpTmpFiles()
{
  for (int i=0; i<_tmpFiles.Size(); ++i)
  {
    const RString& name = _tmpFiles[i];
    QIFileFunctions::CleanUpFile(name);
  }
  _tmpFiles.Clear();
}


void TestXMLToCSV()
{
  const RString xmlName = "O:\\ca\\languagemissions\\stringtable.xml";
  const RString csvName = "O:\\ca\\languagemissions\\stringtablePokus.csv";

  bool result = PBORepack::SaveStringtableXMLAsCSV(xmlName, csvName);
  GLanguage = "English";
  result = LoadStringtable("Pokus", csvName);

}

void TestXMLToBIN()
{
  const RString xmlName = "O:\\languagecore\\stringtable.xml";
  const RString binName = "O:\\languagecore\\stringtable.bin";

  bool result = PBORepack::SaveStringtableXMLAsBIN(xmlName, binName);
  GLanguage = "English";
  result = LoadStringtable("Pokus", binName);
}

int _tmain(int argc, _TCHAR* argv[])
{
   // command line state
  enum ECmdState
  {
    CSNormal, //< no option
    CSPBOOutput, //< output pbo file
    CSTexHeader, //< tex header file
    CSLogFile, //< log file
    CSPBOPRefix //< PBO prefix
  };

  // command line state
  ECmdState cmdState = CSNormal;

  // source folder
  RString srcFolder;
  // output PBO file name
  RString pboOutput;
  // output texture header file name
  RString texHeaderOutput;
  // log file name
  RString logFileName;
  // pbo prefix
  RString pboPrefix;

  // if we should convert stringtable.xml to stringtable.csv
  bool convertXMLToCSV = false;
  // if we should convert stringtable.xml to stringtable.bin
  bool convertXMLToBIN = false;

  // parse command line
  for (int i=1; i<argc; ++i)
  {
    const char* txt = argv[i];
    size_t txtLen = strlen(txt);

    switch (cmdState)
    {
    case CSNormal:
      if (txtLen == 2 && txt[0] == '-')
      {
        switch(tolower(txt[1]))
        {
        case 'o':
          cmdState = CSPBOOutput;
          break;
        case 't':
          cmdState = CSTexHeader;
          break;
        case 'l':
          cmdState = CSLogFile;
          break;
        case 'p':
          cmdState = CSPBOPRefix;
          break;
        case 'x':
          convertXMLToCSV = true;
          cmdState = CSNormal;
          break;
        case 'b':
          convertXMLToBIN = true;
          cmdState = CSNormal;
          break;
        }
      }
      else
      {
        if (srcFolder.IsEmpty())
          srcFolder = txt;
      }
      break;
    case CSPBOOutput:
      pboOutput = txt;
      cmdState = CSNormal;
      break;
    case CSTexHeader:
      texHeaderOutput = txt;
      cmdState = CSNormal;
      break;
    case CSLogFile:
      logFileName = txt;
      cmdState = CSNormal;
      break;
    case CSPBOPRefix:
      pboPrefix = txt;
      cmdState = CSNormal;
      break;
    default:
      cmdState = CSNormal;
      break;
    }//switch
  }//for

  // check if cmd line is ok
  bool badCmdLine = true;

  if (!srcFolder.IsEmpty() && !pboOutput.IsEmpty() && !pboPrefix.IsEmpty())
  {
    badCmdLine = false;
  }
  if (!srcFolder.IsEmpty() && !pboPrefix.IsEmpty() && !texHeaderOutput.IsEmpty())
  {
    badCmdLine = false;
  }

  if (badCmdLine)
  {
    printf(
      "XBOX PBO Repacker.\n"
      "Usage:\n\n"
      "XBOXPBORepack.exe src_folder -X -B -O output_pbo -P pbo_prefix -L log_file -T texture_header_file\n\n"
      "Where\n"
      "-X...convert all stringtable.xml files to csv (cannot be used with -B)"
      "-B...convert all stringtable.xml files to bin (cannot be used with -X)"
      "src_folder...source folder to pack (example  - o:\\ca)\n"
      "output_pbo...output pbo file name (example  - o:\\all.pbo)\n"
      "pbo_prefix...prefix of the PBO (example - ca)\n"
      "log_file...optional, name of log with file names, these files will be packed in the same order as they are in the log.\n"
      "texture_header_file...optional, output file for texture headers.\n"
      "\nExamples:\n"
      "Pack source folder\n"
      "XBOXPBORepack.exe o:\\ca -O o:\\all.pbo -P ca \n\n"
      "Pack source folder, use log file and create texture header file:\n"
      "XBOXPBORepack.exe o:\\ca -O o:\\all.pbo -P ca -L o:\\filelog.txt -T o:\\headers.thd\n\n"
      "Do not pack, just create headers file:\n"
      "XBOXPBORepack.exe o:\\ca -P ca -T o:\\headers.thd\n\n"
      );

  }
  else if (convertXMLToBIN && convertXMLToCSV)
  {
    printf("-X and -B options can not be used simultaneously.\n");
  }
  else
  {
    PBORepack packer;
    PBORepack::STConvertType convertType = PBORepack::STCNone;
    if (convertXMLToCSV)
      convertType = PBORepack::STCCSV;
    else if (convertXMLToBIN)
      convertType = PBORepack::STCBIN;

    if (packer.Pack(srcFolder, pboOutput, logFileName, pboPrefix, texHeaderOutput, convertType))
    {
      printf("DONE!\n");
    }
    else
    {
      printf("FAILED!\n");
    }
  }

  printf("Press any key to continue!\n");

  getch();
  return 0;
}


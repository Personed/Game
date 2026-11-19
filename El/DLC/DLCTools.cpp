#include <El/elementpch.hpp>
#include "DLCTools.h"
#include "../QStream/qbStream.hpp"
#include "../QStream/qStream.hpp"
#include <Es/Files/fileContainer.hpp>
#include <Es/platform.hpp>
#include <tchar.h>

#if _ENABLE_DLC_SUPPORT

namespace DLCTools
{
  // name of the directory with DLC setup files
  static const char *dlcDir = "DLCSetup";

  // name of the file with version of the installation files
  static const char *setupVersionFile = "verSetup.txt";

  // name of the file with version of the installed files (game files)
  static const char *installedVersionFile = "verGame.txt";

  // maximum length of the string with version
  static const int maxLen = 20;

  static HWND setupWndHandle = NULL;

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

  // reads version from the file
  int GetVersionFromFile(RString fileName)
  {
    QIFStream file;
    char buf[maxLen];

    LogF("Loading version from: %s", cc_cast(fileName));
    file.open(fileName);
    if (file.fail())
    {
      LogF("Can't open file: %s", cc_cast(fileName));
      if (GFileServerFunctions)
        GFileServerFunctions->FlushReadHandle(fileName);
      return 0;
    }

    file.readLine(buf, maxLen);
    file.close();

    if (GFileServerFunctions)
      GFileServerFunctions->FlushReadHandle(fileName);

    int version = atoi(buf);
    LogF("Version %d loaded from file %s", version, cc_cast(fileName));
    return version;
  }

  BOOL CALLBACK EnumDialogWindows(HWND wHandle, LPARAM lParam)
  {
    // find the setup window. 
    HWND window = FindWindowEx(wHandle, NULL, _T("BIS Setup"),NULL);
    if (window)
    {
      setupWndHandle = window;
      return false;
    }
    return true;
  }

  bool CheckDLCData(RString path)
  {
    LogF("Checking files in %s.", cc_cast(path));

    QIFStream file;
    const int maxLine = 255;
    char buf[maxLine];

    RString fileName = path + "\\checkFileList.lst";

    LogF("Loading list file: %s", cc_cast(fileName));
    file.open(fileName);
    if (file.fail())
    {
      LogF("Can't open file: %s", cc_cast(fileName));
      if (GFileServerFunctions)
        GFileServerFunctions->FlushReadHandle(fileName);
      return false;
    }
          
    while (file.readLine(buf, maxLine))
    {
      RString line = buf;
      RString fileNameText;
      RString sizeText;

      int divideIndex = line.Find('?', 0);
      if (divideIndex < 0)
      {
        RptF("Can't find '?' char in the line: %s", cc_cast(line));
        file.close();
        if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileName);
        return false;
      }

      fileNameText = line.Substring(0, divideIndex);
      sizeText = line.Substring(divideIndex + 1, line.GetLength());
      QFileSize listFileSize = atol(sizeText.Data());

      LogF("Trying to open file: %s", cc_cast(fileNameText));
      QIFStream testFile;
      testFile.open(fileNameText);
      if (testFile.fail())
      {
        RptF("Check failed, unable to open file: %s", cc_cast(fileNameText));
        testFile.close();
        file.close();
        if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileName);
        if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileNameText);
        return false;
      }

      QFileSize fileSize = testFile.GetDataSize();
      LogF("Comparing file sizes. checkFile: %d - target file: %d", listFileSize, fileSize);
      if (fileSize != listFileSize)
      {
        RptF("Check failed, sizes are different");
        testFile.close();
        file.close();
        if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileName);
        if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileNameText);
        return false;
      }

      LogF("File is ok.");

      testFile.close();
      if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileNameText);
    }

    file.close();
    if (GFileServerFunctions) GFileServerFunctions->FlushReadHandle(fileName);

    LogF("File check complete");
    return true;
  }

  bool IsCheckNeeded(RString path)
  {
    // read setup and game version of the dlc
    int setupVersion = GetVersionFromFile(path + "\\" + setupVersionFile);
    int gameVersion = GetVersionFromFile(path + "\\" + installedVersionFile);

    // check that setup version is correct
    if (setupVersion == 0)
    {
      LogF("Can't load setup version.");
      return false;
    }

    // check if data in DLC are undamaged
    if (CheckDLCData(path))      
    {
      // check if the game version is ok
      if (gameVersion != 0)
      {
        // compare versions if there is a new one
        if (gameVersion >= setupVersion)
        {
          LogF("DLC is up to date.");
          return false;
        }
      }
    }
    return true;
  }

  // checks if the DLC in given directory has been updated and calls setup.exe to update addons in game directory
  bool CheckVersions(RString path)
  {
    RString setupPath = "\"" + path + "\\datacachepreprocessor.exe\" -doNotRunM";

    if (!IsCheckNeeded(path))
      return false;

    LogF("DLC needs update, starting installation process.");
    // now start the setup.exe and wait until it's done
    STARTUPINFO         startupInfo;
    PROCESS_INFORMATION processInfo;

    memset(&startupInfo, 0, sizeof(startupInfo));
    memset(&processInfo, 0, sizeof(processInfo));
    startupInfo.cb = sizeof(startupInfo);
 
    LogF("Starting process: %s", cc_cast(setupPath));
#ifdef UNICODE
    WCHAR wBuf[_MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, setupPath, -1, wBuf, lenof(wBuf));
    if(CreateProcess(wBuf, _T(""), 0, 0, FALSE, CREATE_DEFAULT_ERROR_MODE, 0, 0, &startupInfo, &processInfo) == FALSE)
      return false;
#else
    char tmpPath[2048];
    strcpy(tmpPath, setupPath.Data());
    if(CreateProcess(NULL, tmpPath, 0, 0, FALSE, CREATE_DEFAULT_ERROR_MODE, 0, path, &startupInfo, &processInfo) == FALSE)
    {
      RptF("Starting process failed.");
      return false;
    }
#endif // !UNICODE

    LogF("Waiting for installation to end.");

    // wait until the process is finished
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    // now wait a moment to be sure that setup starts properly and we will be able to detect class name from the GUI.
    Sleep(3000);

    // because some protection systems (Impulse, steam...) starts setup.exe as different proccess after the check, 
    // make sure that the window is really closed and there is no setup running

    LogF("Looking for setup process.");

    if (EnumWindows(EnumDialogWindows, NULL))
    {
      LogF("Setup is not running, update complete.");
      return true;
    }

    if (setupWndHandle != NULL)
    {
      DWORD processId;
      // now retrieve process ID
      GetWindowThreadProcessId(setupWndHandle, &processId);

      // and now get process handle
      HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, processId);
      if (hProcess != NULL)
      {
        // wait until it's closed
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
      }
    }

    LogF("Setup process closed, update complete.");

    return true;
  }


  // finds all DLCs in DLC directory and runs setup if there is a new version of DLC
  void CheckDLCForUpdate()
  {
    LogF("Checking DLC for update.");
    RString currentDir;
#ifdef UNICODE
    TCHAR setupSourceDir[_MAX_PATH];
    int size = GetCurrentDirectory(_MAX_PATH, setupSourceDir);   
    currentDir = LPCTSTR2RString(setupSourceDir, size);
#else
    int size = GetCurrentDirectory(0, NULL);
    GetCurrentDirectory(size, currentDir.CreateBuffer(size));
#endif

    RString wildName = currentDir + "\\" + dlcDir;
    _finddata_t find;
    long hf = X_findfirst(wildName + "\\*",&find);  
    if (hf!=-1)
    {
      do
      {
        if ((find.attrib&_A_SUBDIR) == 0 || !strcmp(find.name,".") || !strcmp(find.name,"..")) continue;

        RString newPath = wildName + "\\" + find.name;

        LogF("Checking DLC: %s", cc_cast(newPath));
        CheckVersions(newPath);
      } while (_findnext(hf,&find)==0);
    }

    LogF("DLC check complete.");
  }

  // Saves a version from setup version file in game version file 
  void SaveCurrentVersionFile(RString setupDir)
  {
    RString versionFile = setupDir + "\\" + setupVersionFile;
    RString installedFile =  setupDir + "\\" + installedVersionFile;

    int currentVersion = GetVersionFromFile(versionFile);
    char str[32];

#ifdef _WIN32
    itoa(currentVersion,str,10);
#else
    sprintf(str,"%d",currentVersion);
#endif

    QOFStream file;
    file.open(installedFile);
    file.write(str, maxLen);
    file.close();
  }
}

#endif //_ENABLE_DLC_SUPPORT

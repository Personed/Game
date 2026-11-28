# Microsoft Developer Studio Project File - Name="buldozer" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 60000
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=buldozer - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "buldozer.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "buldozer.mak" CFG="buldozer - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "buldozer - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "buldozer - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE "buldozer - Win32 Testing" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/Poseidon/buldozer", QJAAAAAA"
# PROP Scc_LocalPath "."
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../Release"
# PROP Intermediate_Dir "../Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /G6 /W3 /GR /Zi /Og /Oi /Os /Oy /Ob1 /Gf /Gy /I "w:\c" /D __MSC__=1100 /D _RELEASE=1 /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /YX"wpch.hpp" /FD /c
# SUBTRACT CPP /Ox
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD BASE RSC /l 0x405 /d "NDEBUG"
# ADD RSC /l 0x405 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386
# ADD LINK32 Delayimp.lib vorbisfile.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dsound.lib ddraw.lib dxguid.lib dinput.lib rsa.lib largeint.lib /nologo /subsystem:windows /map:"x:\buldoTest.map" /debug /machine:I386 /nodefaultlib:"libcmt.lib" /out:"x:\buldoTest.exe" /DELAYLOAD:glide3x.dll /DELAYLOAD:mpiwin32.dll
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../Debug"
# PROP Intermediate_Dir "../Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /G6 /W3 /GR /Zi /Od /I "w:\c" /D __MSC__=1100 /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /YX"wpch.hpp" /FD /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o "NUL" /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o "NUL" /win32
# ADD BASE RSC /l 0x405 /d "_DEBUG"
# ADD RSC /l 0x405 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 Delayimp.lib vorbisfiled.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dsound.lib ddraw.lib dxguid.lib dinput.lib rsa.lib largeint.lib /nologo /subsystem:windows /incremental:no /debug /machine:I386 /nodefaultlib:"libcmt.lib" /pdbtype:sept /DELAYLOAD:glide3x.dll /DELAYLOAD:mpiwin32.dll
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "buldozer"
# PROP BASE Intermediate_Dir "buldozer"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Testing"
# PROP Intermediate_Dir "Testing"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /G5 /MT /W3 /GR /Zi /O2 /D __MSC__=1100 /D _RELEASE=1 /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /YX"wpch.hpp" /FD /c
# ADD CPP /nologo /G5 /MT /W3 /GR /Zi /O2 /I "w:\c" /D _RELEASE=0 /D "WIN32" /D "_WINDOWS" /D __MSC__=1100 /YX"wpch.hpp" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o "NUL" /win32
# ADD BASE RSC /l 0x405 /d "NDEBUG"
# ADD RSC /l 0x405 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 glide3x.lib kernel32.lib user32.lib gdi32.lib winspool.lib winmm.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dsound.lib ddraw.lib dxguid.lib dinput.lib /nologo /subsystem:windows /debug /machine:I386 /out:"x:\buldozer.exe"
# SUBTRACT BASE LINK32 /profile
# ADD LINK32 glide3x.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dsound.lib ddraw.lib dxguid.lib dinput.lib rsa.lib /nologo /subsystem:windows /debug /machine:I386 /out:"\\intel\objview\debug\buldozerTst.exe" /DELAYLOAD:mpiwin32.dll
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "buldozer - Win32 Release"
# Name "buldozer - Win32 Debug"
# Name "buldozer - Win32 Testing"
# Begin Group "D3D8"

# PROP Default_Filter ""
# Begin Group "Shaders8"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayDetail.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayDetailSpecular.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayDetailSpecularAlpha.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayGrass.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayNormal.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDaySpecular.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDaySpecularAlpha.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psDayWater.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightDetail.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightDetailSpecular.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightDetailSpecularAlpha.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightGrass.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightNormal.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightSpecular.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightSpecularAlpha.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\psNightWater.psa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\Shaders\vsDayGrass.vsa

!IF  "$(CFG)" == "buldozer - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# Begin Group "ShaderHeaders8"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\D3D8\Shaders\psDecl.h
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=..\lib\D3D8\Shaders\psNightFragment.inc
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=..\lib\D3D8\Shaders\Shaders.txt
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=..\lib\D3D8\Shaders\vsDecl.h
# PROP Exclude_From_Build 1
# End Source File
# End Group
# Begin Source File

SOURCE=..\lib\d3d8\d3dDefs.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\engD3D.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /Ot
# SUBTRACT BASE CPP /Os
# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\engdd.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /Ot
# SUBTRACT BASE CPP /Os
# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\lib\d3d8\engdd.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\D3D8\txtD3D.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /Ot
# SUBTRACT BASE CPP /Os
# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\lib\d3d8\txtd3d.hpp
# End Source File
# End Group
# Begin Group "Glide"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\engGlide.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# ADD CPP /Od

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /Ot
# SUBTRACT BASE CPP /Os
# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\lib\engGlide.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\extGlide.h
# End Source File
# Begin Source File

SOURCE=..\LIB\txtGlide.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# ADD CPP /Od /Ob0

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /Ot
# SUBTRACT BASE CPP /Os
# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\lib\txtGlide.hpp
# End Source File
# End Group
# Begin Group "Essence"

# PROP Default_Filter ""
# Begin Group "Algorithms"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Algorithms\bSearch.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Algorithms\crc32.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Algorithms\crc32.h
# End Source File
# Begin Source File

SOURCE=..\..\Es\Algorithms\qsort.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Algorithms\splineEqD.hpp
# End Source File
# End Group
# Begin Group "EssenceCommon"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Common\fltopts.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Common\global.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Common\mathND.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Common\win.h
# End Source File
# End Group
# Begin Group "Containers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Containers\array.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\array2D.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\bankArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\bankInitArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\bigArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\bitmask.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\bitmask.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\boolArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\cachelist.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\compactBuf.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\forEach.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\hashMap.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\list.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\listBidir.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\maps.cpp
# ADD CPP /I "w:\c\Hierarchy"
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\maps.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\quadtree.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\rStringArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\smallArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\sortedArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\staticArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\streamArray.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\typeDefines.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\typeGenTemplates.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\typeOpts.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Containers\typeTemplates.hpp
# End Source File
# End Group
# Begin Group "Framework"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Framework\appFrame.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\appFrame.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\debugLog.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\logflags.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\netlog.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\netlog.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\optDefault.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\optEnable.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\potime.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Framework\potime.hpp
# End Source File
# End Group
# Begin Group "Memory"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Memory\checkMem.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\debugNew.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\defNew.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\defNormNew.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\fastAlloc.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\fastAlloc.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\memAlloc.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Memory\normalNew.hpp
# End Source File
# End Group
# Begin Group "Strings"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Strings\bstring.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Strings\rString.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\Es\Strings\rString.hpp
# End Source File
# End Group
# Begin Group "Types"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Types\enum_decl.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\lLinks.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\lLinks.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\memtype.h
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\pointers.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\removeLinks.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\scopeLock.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\softLinks.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Types\softLinks.hpp
# End Source File
# End Group
# Begin Group "Threads"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Threads\multisync.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\pocritical.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\pocritical.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\posemaphore.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\posemaphore.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\pothread.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\pothread.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\threadSync.cpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Threads\threadSync.hpp
# End Source File
# End Group
# Begin Group "Files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\Es\Files\commandLine.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Files\fileContainer.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\Files\filenames.hpp
# End Source File
# End Group
# Begin Source File

SOURCE=..\..\Es\essencepch.hpp
# End Source File
# Begin Source File

SOURCE=..\..\Es\platform.hpp
# End Source File
# End Group
# Begin Group "Element"

# PROP Default_Filter ""
# Begin Group "PCH"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\PCH\afxConfig.h
# End Source File
# Begin Source File

SOURCE=..\..\El\PCH\ext_options.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\PCH\libIncludes.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\PCH\normalConfig.h
# End Source File
# Begin Source File

SOURCE=..\..\El\PCH\sReleaseConfig.h
# End Source File
# Begin Source File

SOURCE=..\..\El\PCH\stdIncludes.h
# End Source File
# End Group
# Begin Group "ParamFile"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\ParamFile\classDbParamFile.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\classDbParamFile.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFile.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFile.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileCtx.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileCtx.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileUseEvalExpress.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileUseLocalizeStringtable.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileUsePreprocC.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamFile\paramFileUseSumCalcCrc.cpp
# End Source File
# End Group
# Begin Group "Evaluator"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Evaluator\evaluatorExpress.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Evaluator\evaluatorExpress.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Evaluator\evalUseLocalizeStringtable.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Evaluator\express.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\Evaluator\express.hpp
# End Source File
# End Group
# Begin Group "Common"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Common\globalAlive.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /YX"elementpch.hpp"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Common\isaac.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Common\perfLog.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Common\randomGen.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\Common\randomGen.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Common\randomJames.cpp
# ADD CPP /I "w:\c\Hierarchy"
# End Source File
# Begin Source File

SOURCE=..\..\El\Common\randomJames.h
# End Source File
# End Group
# Begin Group "QStream"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\QStream\fileCompress.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\fileCompress.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\fileinfo.h
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\fileMapping.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\fileMapping.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\fileOverlapped.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\qstream\fileOverlapped.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\iQFBank.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\qbstream.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\QBStream.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\qstream.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\QStream.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\qstreamUseBankQFBank.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\serializeBin.cpp
# ADD CPP /YX"../elementpch.hpp"
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\serializeBin.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\QStream\ssCompress.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2 /YX"../elementpch.hpp"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

# ADD CPP /YX"../elementpch.hpp"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD CPP /YX"../elementpch.hpp"

!ENDIF 

# End Source File
# End Group
# Begin Group "PreprocC"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\PreprocC\Preproc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\PreprocC\Preproc.h
# End Source File
# Begin Source File

SOURCE=..\..\El\PreprocC\preprocC.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\PreprocC\preprocC.hpp
# End Source File
# End Group
# Begin Group "Stringtable"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Stringtable\localizeStringtable.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Stringtable\localizeStringtable.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Stringtable\stringtable.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Stringtable\stringtable.hpp
# End Source File
# End Group
# Begin Group "XML"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\XML\inet.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\XML\inet.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\XML\xml.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /YX"elementpch.hpp"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\XML\xml.hpp
# End Source File
# End Group
# Begin Group "ElementNetwork"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Network\netapi.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netapi.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netchannel.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netchannel.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netglobal.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netmessage.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netmessage.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpch.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpeer.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpeer.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpeerxbox.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpeerxbox.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netpool.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\netXboxConfig.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\peerfactory.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\peerfactory.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\peerfactoryxbox.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Network\peerfactoryxbox.hpp
# End Source File
# End Group
# Begin Group "Calendar"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Calendar\pocalendar.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Calendar\pocalendar.hpp
# End Source File
# End Group
# Begin Group "Modules"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Modules\modules.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Modules\modules.hpp
# End Source File
# End Group
# Begin Group "Math"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Math\math3d.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dK.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dK.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dP.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dP.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3DPK.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dT.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\math3dT.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\mathDefs.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\mathOpt.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\mathOpt.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\matrix.asm
# End Source File
# Begin Source File

SOURCE=..\..\El\Math\matrixP3.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\El\Math\vecTempl.hpp
# End Source File
# End Group
# Begin Group "FreeOnDemand"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\FreeOnDemand\memFreeReq.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\FreeOnDemand\memFreeReq.hpp
# End Source File
# End Group
# Begin Group "Interfaces"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Interfaces\iAppInfo.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Interfaces\iClassDb.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Interfaces\iEval.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Interfaces\iLocalize.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Interfaces\iPreproc.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Interfaces\iSumCalc.hpp
# End Source File
# End Group
# Begin Group "CRC"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\CRC\crc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\CRC\crc.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\CRC\sumCalcCrc.hpp
# End Source File
# End Group
# Begin Group "ParamArchive"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\ParamArchive\paramArchive.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamArchive\paramArchive.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamArchive\paramArchiveDb.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamArchive\paramArchiveDb.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamArchive\paramArchiveUseDbParamFile.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\ParamArchive\serializeClass.hpp
# End Source File
# End Group
# Begin Group "Clipboard"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Clipboard\clipboard.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Clipboard\clipboard.hpp
# End Source File
# End Group
# Begin Group "Debugging"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Debugging\debugTrap.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Debugging\debugTrap.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Debugging\imexhnd.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Debugging\imexhnd.h
# End Source File
# Begin Source File

SOURCE=..\..\El\Debugging\mapFile.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Debugging\mapFile.hpp
# End Source File
# End Group
# Begin Group "Color"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Color\colors.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Color\colorsFloat.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Color\colorsK.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Color\colorsK.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Color\colorsP.cpp
# End Source File
# End Group
# Begin Group "Time"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Time\time.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Time\time.hpp
# End Source File
# End Group
# Begin Group "DebugWindow"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\DebugWindow\debugWin.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\DebugWindow\debugWin.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\DebugWindow\debugWinImpl.hpp
# End Source File
# End Group
# Begin Group "Enum"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Enum\dynEnum.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Enum\dynEnum.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Enum\enumNames.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Enum\enumNames.hpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Enum\idString.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Enum\idString.hpp
# End Source File
# End Group
# Begin Group "Statistics"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Statistics\statistics.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Statistics\statistics.hpp
# End Source File
# End Group
# Begin Group "HierWalker"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\HierWalker\hierWalker.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\HierWalker\hierWalker.hpp
# End Source File
# End Group
# Begin Group "Breakpoint"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\El\Breakpoint\breakpoint.cpp
# End Source File
# Begin Source File

SOURCE=..\..\El\Breakpoint\breakpoint.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\..\El\elementpch.hpp
# End Source File
# End Group
# Begin Group "Library"

# PROP Default_Filter ""
# Begin Group "Headers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\animation.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\camEffects.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\camera.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\CdaPfn.h
# End Source File
# Begin Source File

SOURCE=..\lib\cdaPfnCond.h
# End Source File
# Begin Source File

SOURCE=..\lib\clip2d.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\clipShape.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\clipVert.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\data3d.h
# End Source File
# Begin Source File

SOURCE=..\lib\detector.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\dikCodes.h
# End Source File
# Begin Source File

SOURCE=..\lib\dynSound.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Eax.h
# End Source File
# Begin Source File

SOURCE=..\lib\edges.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\engine.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\engineDll.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\fileServer.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\fileServerMT.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\font.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\fsm.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\global.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\handledList.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\heap.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\interpol.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Joystick.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\keyInput.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\keyLights.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\landFile.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\landscape.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\languages.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\lights.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\lockCache.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\mapTypes.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\memcheck.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\MemGrow.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\object.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\objectClasses.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\occlusion.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\pactext.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\pathSteer.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\perfProf.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\plane.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\poly.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\polyClip.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\quatrix.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\roads.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\rtAnimation.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\samples.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\saveVersion.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\scene.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\scripts.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\sentences.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\serial.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Shape.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\speaker.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\textbank.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\titEffects.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\tlVertex.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\types.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\vertex.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\visibility.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\visual.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\weapons.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\winpch.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\world.hpp
# End Source File
# End Group
# Begin Group "Network"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\Network\messageFactory.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Network\netTransport.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\netTransportDPlay.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\netTransportNet.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\network.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Network\network.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\networkClient.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Network\networkImpl.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\networkMsg.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Network\networkObject.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Network\networkServer.cpp
# End Source File
# End Group
# Begin Group "Mission"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\arcadeTemplate.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\arcadeTemplate.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\ArcadeWaypoint.hpp
# End Source File
# End Group
# Begin Group "Sound"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\riffFile.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\soundDX8.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\soundDX8.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\soundScene.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\soundScene.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\soundSys.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\soundsys.hpp
# End Source File
# End Group
# Begin Group "UI"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\autoComplete.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\autoComplete.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\chat.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\chat.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\displayUI.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\displayUI.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\optionsUI.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\optionsUI.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\resincl.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\uiActions.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiArcade.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiContainers.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiControls.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\uiControls.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiControlsExt.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiMap.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\uiMap.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiMapExport.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\uiMapExt.cpp
# End Source File
# End Group
# Begin Group "Cloth"

# PROP Default_Filter ""
# Begin Group "Cloth Sources"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\Cloth\ClothObject.cpp
# End Source File
# End Group
# Begin Group "Cloth Headers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\Cloth\ClothObject.h
# End Source File
# End Group
# End Group
# Begin Group "Vehicles"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\airplane.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\airplane.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\allAIVehicles.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\allVehicles.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\camera.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\cameraHold.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\cameraHold.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\car.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\car.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\detector.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\dynSound.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\envTracker.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\envTracker.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\fireplace.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\gearBox.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\gearBox.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\head.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\head.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\helicopter.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\helicopter.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\house.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\house.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\indicator.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\indicator.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\laserTarget.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\laserTarget.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\memberCalls.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\motorcycle.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\motorcycle.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\parachute.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\parachute.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\person.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\person.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\pilotHead.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\proxyWeapon.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\proxyWeapon.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\randomShape.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\randomShape.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\recoil.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\recoil.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\seaGull.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\seaGull.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\ship.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\ship.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\shots.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\shots.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\simul.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\smokes.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\smokes.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\soldierOld.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\soldierOld.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\tank.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\tank.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\tankOrCar.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\tankOrCar.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\target.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\thing.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\thing.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\tracks.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\tracks.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\transport.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\transport.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\vehicle.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\vehicleAI.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\vehicleAI.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\vehicleTypes.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\vehSupply.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\vehSupply.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\wounds.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\wounds.hpp
# End Source File
# End Group
# Begin Group "Sources"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\camEffects.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\dammage.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\edges.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\engine.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\engineDraw.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\fileServer.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\fireplace.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\fixed.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\font.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\fontDraw.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\frame.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\frameInv.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\frameInv.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\fsm.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\geography.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\interpol.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\invisibleVeh.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\invisibleVeh.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Joystick.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\keyLights.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\landSave.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\lights.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\mainCommon.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\mainHelper.cpp
# End Source File
# Begin Source File

SOURCE=..\cfg\manActions.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\manActs.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\MemGrow.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\memWin32.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\move_actions.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\networks.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\objectClasses.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\objLine.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\objLine.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\objTrash.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\onExit.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\onExitLib.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\packFiles.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\packFiles.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\pactext.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\pathSteer.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\perfLog.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\plane.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\poly.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\profiler.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\progress.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\progress.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\roads.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\scripts.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\serial.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Shape.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\shapeFile.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\shapeLand.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\speaker.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\SpecLods.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\titEffects.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\txtBank.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\txtPreload.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\txtPreload.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\version.ver
# End Source File
# Begin Source File

SOURCE=..\LIB\vertex.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\viewer.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\visibility.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\vtuneProf.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\vtuneProf.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\wave.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\weapons.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\world.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\wpch.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Yc"wpch.hpp"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# End Group
# Begin Group "VerConfig"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\afxConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\dedServerConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\demoConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\desReleaseConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\mpDemoConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\mpTestConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\mpTestServerConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\normalConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\resistanceConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\resistanceServerConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\sReleaseConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\VBS1Config.h
# End Source File
# Begin Source File

SOURCE=..\lib\VBS1DemoConfig.h
# End Source File
# Begin Source File

SOURCE=..\lib\wpch.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\xboxConfig.h
# End Source File
# End Group
# Begin Group "InGameUI"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\UI\inGameUI.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\inGameUI.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\UI\inGameUIDraw.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\UI\InGameUIImpl.hpp
# End Source File
# End Group
# Begin Group "Global Headers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\vkCodes.h
# End Source File
# End Group
# Begin Group "Checksum"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\integrity.hpp
# End Source File
# End Group
# Begin Group "JPG"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\jpgImport.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\jpgImport.hpp
# End Source File
# End Group
# Begin Group "GameSpy"

# PROP Default_Filter ""
# Begin Group "queryreporting"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\GameSpy\queryreporting\gqueryreporting.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\queryreporting\gqueryreporting.h
# End Source File
# End Group
# Begin Group "cengine"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\GameSpy\cengine\goaceng.h
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\cengine\gserver.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\cengine\gserver.h
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\cengine\gserverlist.c
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\cengine\gutil.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\cengine\gutil.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\LIB\GameSpy\darray.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\darray.h
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\hashtable.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\hashtable.h
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\md5.h
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\md5c.c
# End Source File
# Begin Source File

SOURCE=..\LIB\GameSpy\nonport.c
# End Source File
# Begin Source File

SOURCE=..\lib\GameSpy\nonport.h
# End Source File
# End Group
# Begin Group "MBCS"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\mbcs.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\mbcs.hpp
# End Source File
# End Group
# Begin Group "ParamFileExt"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\paramFileExt.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\paramFileExt.hpp
# End Source File
# End Group
# Begin Group "QStreamExt"

# PROP Default_Filter ""
# Begin Group "Encryption"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\Encryption\VBS1.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Encryption\XOR1024.cpp
# End Source File
# End Group
# End Group
# Begin Group "EvaluatorExt"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\gameStateExt.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\gameStateExt.hpp
# End Source File
# End Group
# Begin Group "StringtableExt"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\stringids.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\stringtableExt.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\stringtableExt.hpp
# End Source File
# End Group
# Begin Group "Optimize"

# PROP Default_Filter ""
# Begin Group "Speed"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\animation.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\clipdraw.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\clipShape.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\collisions.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\drawpoly.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\landscape.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\lockCache.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\memTable.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\object.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\occlusion.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\rtAnimation.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\scene.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\shadow.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\ShapeDraw.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /Ot
# SUBTRACT CPP /Os

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\StdLib.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\LIB\transLight.cpp

!IF  "$(CFG)" == "buldozer - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "buldozer - Win32 Debug"

!ELSEIF  "$(CFG)" == "buldozer - Win32 Testing"

# ADD BASE CPP /O2
# ADD CPP /O2

!ENDIF 

# End Source File
# End Group
# Begin Group "Header"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\MemHeap.hpp
# End Source File
# End Group
# Begin Group "Core"

# PROP Default_Filter ""
# End Group
# End Group
# Begin Group "ASE"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\ASE\ASEMasterSDK.c
# End Source File
# Begin Source File

SOURCE=..\lib\ASE\ASEMasterSDK.h
# End Source File
# Begin Source File

SOURCE=..\lib\ASE\readme.txt
# End Source File
# End Group
# Begin Group "ParamArchiveExt"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\paramArchiveExt.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\paramArchiveExt.hpp
# End Source File
# End Group
# Begin Group "AI"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\lib\AI\ai.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiArcade.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiCenter.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\aiDefs.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiGroup.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\aiPathPlanner.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiRadio.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\aiRadio.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiSubgroup.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiSubgroupFSM.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\aiTypes.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\aiUnit.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\aStar.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\operMap.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\operMap.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\AI\pathPlanner.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\AI\pathPlanner.hpp
# End Source File
# End Group
# Begin Group "Marina"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\LIB\Marina\CarCollisions.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\CarCollisions.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\DampedSpring.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Marina\Dantzig.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\Dantzig.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Marina\Logging.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\Logging.h
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\marina.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\MarinaArray.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\Marina\MatrixA.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\Marina\mtl_lu_solve.cpp
# End Source File
# End Group
# Begin Source File

SOURCE=..\lib\appFrameExt.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\drawText.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\drawText.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\fileLocator.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\fileLocator.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\keyInput.cpp
# End Source File
# Begin Source File

SOURCE=..\LIB\mapObject.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\mapObject.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\material.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\material.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\objectId.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\objectId.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\objLink.hpp
# End Source File
# Begin Source File

SOURCE=..\LIB\serializeBinExt.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\serializeBinExt.hpp
# End Source File
# End Group
# Begin Source File

SOURCE=..\lib\appInfoOFP.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\editor.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\editor.hpp
# End Source File
# Begin Source File

SOURCE=..\lib\engDummy.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\icon1.ico
# End Source File
# Begin Source File

SOURCE=..\lib\main.cpp
# ADD CPP /I "w:\c\hierarchy"
# End Source File
# Begin Source File

SOURCE=.\mainBuld.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\MessageDll.h
# End Source File
# Begin Source File

SOURCE=..\lib\resource.h
# End Source File
# Begin Source File

SOURCE=..\lib\resource.rc
# End Source File
# Begin Source File

SOURCE=..\lib\resrc1.h
# End Source File
# Begin Source File

SOURCE=..\lib\useAppInfoOFP.cpp
# End Source File
# Begin Source File

SOURCE=..\lib\winapp.cpp
# End Source File
# End Target
# End Project

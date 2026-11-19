// OPFCreatePatch.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <malloc.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <Es/Files/filenames.hpp>
#include <Es/Containers/array.hpp>
#include <Es/Strings/rstring.hpp>

#include <El/ParamFile/paramFile.hpp>

#ifndef _SUPER_RELEASE
#include "patch.hpp"
#include "signatures.hpp"
#endif // #ifndef _SUPER_RELEASE

#include "filemap.hpp"

extern bool wasError;

typedef HANDLE UpdateHandle;

void DisplayErrorMessage()
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
	fprintf( stderr, "%s\n", (const char *)lpMsgBuf);
	// Free the buffer.
	LocalFree( lpMsgBuf );
}

void ProcessPatchMessages()
{

}

/// check if the file need to be include to the patch by the file name
struct IFilter
{
	/// returns true if the file need to be included
	virtual bool operator ()(const char *name) const = 0;
};

void FileToResource(int headlen,const char *name, HANDLE handle, IFilter *filter)
{
	if (filter && !(*filter)(name)) return;

	LogF("Add file %s",name+headlen);

	FileMemMapped file;
	file.Open(name);
	if (file.GetError())
	{
		fprintf(stderr,"Cannot read %s\n",name);
		wasError = true;
		return;
	}

	int size = file.GetSize();
	void *buffer = file.GetRawData();

	char nameRes[1024];
	strcpy(nameRes,name+headlen);
	strupr(nameRes);

	// for updating the resource we need to have the whole file in the memory
	BOOL ok = UpdateResource(
		handle, RT_RCDATA, nameRes,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		buffer, size
	);
	if (!ok)
	{
		// some file 
		HRESULT hr = GetLastError();
		fprintf(stderr,"Error %x updating %s\n",hr,nameRes);
	}

}

static void MemoryToResource
(
	const void *buffer, size_t size, const char *name, HANDLE handle
)
{
	LogF("Add memory file %s",name);

	char nameRes[1024];
	strcpy(nameRes,name);
	strupr(nameRes);

	UpdateResource(
		handle, RT_RCDATA, nameRes,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		(void *)buffer, size
	);


}

static void MakeAllDirs(const char *path)
{
	const char *delim = strchr(path,':');
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

void RemoveDirectoryRecursive(const char *path)
{
	char subpath[1024];
	sprintf(subpath,"%s\\*",path);
	_finddata_t data;
	long h = _findfirst(subpath,&data);
	if (h!=-1)
	{
		do
		{
			if (data.attrib&_A_SUBDIR)
			{
				if (!strcmp(data.name,".") || !strcmp(data.name,"..")) continue;
				strcpy(GetFilenameExt(subpath),data.name);
				RemoveDirectoryRecursive(subpath);
				//rmdir(subpath);
			}
			else
			{
				strcpy(GetFilenameExt(subpath),data.name);
				unlink(subpath);
			}
		}
		while (_findnext(h,&data)==0);
		_findclose(h);
		rmdir(subpath);
	}
	rmdir(path);
}

static bool FileExists(const char *name)
{
	int h = open(name,O_RDONLY|O_BINARY);
	if (h<0) return false;
	close(h);
	return true;
}

static bool ScanVersion(const char *version, int &verMajor, int &verMinor)
{
	verMajor = 0;
	verMinor = 0;
	char *end;
	verMajor = strtoul(version,&end,10);
	if (*end!='.') return false;
	verMinor = strtoul(end+1,&end,10);
	return true;
}

void FilesToResource(int headlen, const char *name, UpdateHandle handle, IFilter *filter)
{
	char buffer[1024];
	sprintf(buffer, "%s\\*.*", name);

	WIN32_FIND_DATA info;
	HANDLE h = FindFirstFile(buffer, &info);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				// FIX - do not delete only current and parent directory
				if (strcmp(info.cFileName, ".") && strcmp(info.cFileName, ".."))
				{
					sprintf(buffer, "%s\\%s", name, info.cFileName);
					FilesToResource(headlen,buffer, handle, filter);
				}
			}
			else
			{
				sprintf(buffer, "%s\\%s", name, info.cFileName);
				// Copy file to resource
				FileToResource(headlen,buffer,handle, filter);
			}
		}
		while (FindNextFile(h, &info));
		FindClose(h);
	}
}

#ifndef _SUPER_RELEASE

struct DistributionInfo
{
	/// internal name of the distribution
	RString id;
	/// initial version from which this distribution started to exist (so distribution spec. files are not new even if these occured first in this version)
	RString initialVersion;
	/// index of the parent distribution in the array
	int parentIndex;
};

TypeIsMovableZeroed(DistributionInfo)

static void LoadDistributions(ParamEntryPar versionInfo, AutoArray<DistributionInfo> &distributions)
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
			info.initialVersion = cls >> "initialVersion";
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

/// Search for the best distribution given file exist for
struct BestDistributionFile
{
	RString _name;
	int _index;
	int _level;

	bool Find(RString filename, AutoArray<DistributionInfo> &distributions, int index);
};

bool BestDistributionFile::Find(RString filename, AutoArray<DistributionInfo> &distributions, int index)
{
	_index = index;
	_level = 0;
	// find the most specific distribution file exist for
	while (_index >= 0)
	{
		_name = filename + RString(".#") + distributions[_index].id;
		if (FileExists(_name)) return true; // found
		_index = distributions[_index].parentIndex;
		_level--;
	}
	// check the base file (no distribution)
	_name = filename;
	if (FileExists(_name)) return true; // found
	// not found
	return false;
}

static bool CheckCreateNew(AutoArray<DistributionInfo> &distributions, RString &file, RString version)
{
	const char *ext = GetFileExt(file);
	if (ext && ext[1]=='#')
	{
		const char *distId = ext+2;
		for (int i=0; i<distributions.Size(); i++)
		{
			if (stricmp(distributions[i].id, distId)==0)
			{
				if (stricmp(version, distributions[i].initialVersion)==0) //it is initial version for this distribution, so return false
					return false;
				else
					return true;
			}
		}
	}
	return true;
}

static void FileCreateIncUpdate
(
	int headlen,const char *ver, const char *last, HANDLE handle, 
	AutoArray<DistributionInfo> &distributions
)
{
	// source directory (current version)
	char recent[1024];
	strcpy(recent,ver);
	*GetFilenameExt(recent) = 0;

	const char *verprefixend = strchr(ver+headlen,'\\');
	const char *lastprefixend = strchr(last+headlen,'\\');

	// destination directory
	char diff[1024];
	char tgtVersion[64];
	strncpy(diff,ver,headlen);
	diff[headlen] = 0;
	strncat(diff,last+headlen+1,lastprefixend-last-headlen-1);
	strcat(diff,"-");
	strncat(diff,ver+headlen+1,verprefixend-ver-headlen-1);
	strcat(diff,"\\");
	strncpy(tgtVersion, ver+headlen+1, verprefixend-ver-headlen-1);
	tgtVersion[verprefixend-ver-headlen-1] = 0;
	const char *nameBody = strchr(recent+headlen,'\\');
	if (!nameBody) return;
	strcat(diff,nameBody+1);
	MakeAllDirs(diff);

	// destination filename (without the distribution extension)
	const char *name = GetFilenameExt(ver);
/*
#if USE_DELTA3_BINARY_DIFF
	RString diffName = RString(diff) + RString(name);  //no extension, as we do not know whether it will be .upd, .del, .new
#else
	RString diffName = RString(diff) + RString(name) + RString(".upd");
#endif
*/
	RString diffName = RString(diff) + RString(name) + RString(".upd");

	FindArray<int> done;
	for (int i=-1; i<distributions.Size(); i++)
	{
		// (-1 is used for no distribution - no extension)

		// find the file to update to (for this distribution)
		BestDistributionFile to;
		if (!to.Find(ver, distributions, i)) continue; // no file found for this distribution

		// find the version some file for this distribution is present in
		BestDistributionFile from;
		if (!from.Find(last, distributions, i))
		{
			bool found = false;
			int major, minor;
			if (ScanVersion(last + headlen + 1, major, minor))
			{
				for (int j=minor-1; j>=0; j--)
				{
					RString older = RString(last, headlen) + Format("_%d.%02d", major, j) + RString(lastprefixend);
					if (from.Find(older, distributions, i))
					{
						// found in this directory
						found = true;
						break;
					}
				}
			}
			if (!found)
			{
				// no file found for this distribution - need to add it
				from._name = last; // does not matter about the filename
				from._index = to._index; // take the distribution name from the target
				from._level = to._level;
			}
		}

		// check if this patch is not done yet
		int betterIndex = to._index;
		if (from._level > to._level) betterIndex = from._index;
		if (done.Find(betterIndex) >= 0) continue;

		// find the result name
		RString result = diffName;
		if (betterIndex >= 0) result = diffName + RString(".#") + distributions[betterIndex].id;

		LogF("Incremental update\n"
			"            from %s\n"
			"            to %s\n"
			"            result %s",
			cc_cast(from._name), cc_cast(to._name), cc_cast(result));

		IPatchAction *action = CreatePatchActionCreate(CheckCreateNew(distributions, result, tgtVersion));
		if (action)
			action->PerformByName(from._name, to._name, result);
		else
		{
			LogF("Incremental update action not created.");
		}
		delete action;
	}
}

// handle parameter not used?
static void FilesCreateIncUpdate
(
 int headlen, const char *verPath, const char *lastVerPath, UpdateHandle handle, 
 AutoArray<DistributionInfo> &distributions
)
{
	char buffer[1024];
	sprintf(buffer, "%s\\*.*", verPath);

	// list of all files without the distribution extension
	FindArrayRStringCI files;

	WIN32_FIND_DATA info;
	HANDLE h = FindFirstFile(buffer, &info);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			// FIX - do not delete only current and parent directory
			if (!strcmp(info.cFileName, ".") || !strcmp(info.cFileName, ".."))
			{
				continue;
			}
			if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				char ver[1024];
				char last[1024];
				sprintf(ver, "%s\\%s", verPath, info.cFileName);
				sprintf(last, "%s\\%s", lastVerPath, info.cFileName);
				FilesCreateIncUpdate(headlen,ver, last, handle, distributions);
			}
			else
			{
				// Add to the list of files
				char *ext = strrchr(info.cFileName, '.');
				if (ext && *(ext + 1) == '#')
				{
					// remove the distribution extension
					files.AddUnique(RString(info.cFileName, ext - info.cFileName));
				}
				else files.AddUnique(info.cFileName);
			}
		}
		while (FindNextFile(h, &info));
		FindClose(h);
	}

	// now create distributions for all files
	for (int i=0; i<files.Size(); i++)
	{
		// Copy file to resource
		char ver[1024];
		char last[1024];
		sprintf(ver, "%s\\%s", verPath, cc_cast(files[i]));
		sprintf(last, "%s\\%s", lastVerPath, cc_cast(files[i]));
		FileCreateIncUpdate(headlen, ver, last, handle, distributions);
	}
}

struct FilterByDistribution : public IFilter
{
	AutoArray<DistributionInfo> &_distributions;

	FilterByDistribution(AutoArray<DistributionInfo> &distributions) : _distributions(distributions) {}
	virtual bool operator ()(const char *name) const
	{
		const char *ext = strrchr(name, '.');
		if (ext && *(ext + 1) == '#')
		{
			// distribution is given
			for (int i=0; i<_distributions.Size(); i++)
			{
				if (stricmp(ext + 2, _distributions[i].id) == 0) return true; // known distribution
			}
			return false; // unknown distribution
		}
		return true; // no distribution - need to include
	}
};

#endif // #ifndef _SUPER_RELEASE

void GetAppVersion(const char *app, char *version)
{
	*version = 0;
	DWORD handle;
	DWORD bufferSize = GetFileVersionInfoSize((char *)app, &handle);
	char *buffer = new char[bufferSize];
	BOOL ok = GetFileVersionInfo((char *)app, handle, bufferSize, buffer);
	void *data = NULL;
	unsigned int dataSize = 0;
	if (ok)
	{
		VerQueryValue(buffer, TEXT("\\StringFileInfo\\040904b0\\FileVersion"), &data, &dataSize);
		strncpy(version, (char *)data, dataSize);
	}
	delete [] buffer;
	version[dataSize] = 0;
}

const char *SkipStart(const char *text, const char *start)
{
	int len = strlen(start);
	if (strnicmp(text,start,len)) return NULL;
	return text+len;
}

int consoleMain(int argc, const char *argv[])
{
	bool exePatch = false;
	bool reuse = false;
	bool nameByVersion = false;
	bool clean = false;
	bool noPack = false;
	if (argc<2)
	{
		fprintf
		(
			stderr,
			"Usage:\n"
			"Special form for creating applicaton patch:\n"
			"\tOPFCreatePatch <exeName>\n"
			"\n"
			"General form:\n"
			"\tOPFCreatePatch <exeName> {<options>} <pattern1> <pattern2> ....\n"
			"\n"
			"Options:\n"
			"\t-patch    create application patch (see below)\n"
#ifndef _SUPER_RELEASE
			"\t-reuse    do not create incremental update\n"
			"\t-clean    remove all incremental update (only if -reuse is not used)\n"
#endif // #ifndef _SUPER_RELEASE
			"\t-name     name to append for target exe\n"
			"\t-version  version is appended to target exe\n"
			"\t-nopack   no target exe created (incremental only)\n"
			"\n"
#ifndef _SUPER_RELEASE
			"Patch uses version information (version.txt and source.txt)\n"
			"from version folder, creates incremental updates\n"
			"and copies directories 'All', 'CRC', 'Version' and incremental updates.\n"
			"\n"
#else // #ifndef _SUPER_RELEASE
			"Patch uses version information (version.txt and source.txt)\n"
			"from version folder and copies directories\n"
			"'All', 'CRC' and 'Version'.\n"
			"\n"
#endif // #ifndef _SUPER_RELEASE #else
			"If -patch is not specified, only folders given in pattern are copied.\n"
		);
		return 1;
	}
	char path[1024];
	char exePath[1024];
	if (strchr(argv[1],'\\'))
	{
		strcpy(exePath,argv[1]);
	}
	else
	{
		getcwd(exePath,sizeof(exePath));
		TerminateBy(exePath,'\\');
		strcat(exePath,argv[1]);
	}
	strcpy(path,exePath);
	strcpy(GetFilenameExt(path),"");
	chdir(path);

	argc--;
	argv++;
	if (argc==1) exePatch = true, nameByVersion = true;
	FindArray<RString> patterns;
	const char *patchname = NULL;
	while (argc>1)
	{
		if (*argv[1]=='-')
		{
			const char *start;
			if (!strcmp(argv[1],"-patch")) exePatch = true;
			else if (!strcmp(argv[1],"-reuse")) reuse = true;
			else if (!strcmp(argv[1],"-clean")) clean = true;
			else if (!strcmp(argv[1],"-version")) nameByVersion = true;
			else if (!strcmp(argv[1],"-nopack")) noPack = true;
			else if (start=SkipStart(argv[1],"-name="))
			{
				patchname = start;
			}
		}
		else
		{
			// scan patterns
			patterns.AddUnique(argv[1]);
		}
		argc--;
		argv++;
	}
	// default pattern: exe patch
#if USE_DELTA3_BINARY_DIFF
	//check whether there is xdelta3.exe present on local directory and fail if not
	if (exePatch)
		if (_access(XDelta3FileName, 0)) //file not exist
		{
			fprintf(stderr,"Error cannot find the %s in the current directory.\n", XDelta3FileName);
			return 1;
		}
#endif

	ParamFile versionInfo;
	versionInfo.Parse("version.cfg");

	int verMajor = 0;
	int verMinor = 0;
	int verMinorRequired = 0;
	if (exePatch || nameByVersion)
	{
		char version[256];
		// read version information from the "source" exe file
		ConstParamEntryPtr entry = versionInfo.FindEntry("source");
		if (entry)
		{
			RString versionExe = *entry;
			if (versionExe.GetLength() > 0)
			{
				GetAppVersion(versionExe,version);
				if (*version) versionInfo.Add("version", version);
			}
		}

		// read version information from "version"
		entry = versionInfo.FindEntry("version");
		if (!entry)
		{
			fprintf(stderr, "'version' entry not found in version.cfg");
			return 1;
		}

		strcpy(version, entry->GetValue());

		char *end;
		verMajor = strtoul(version,&end,10);
		if (*end!='.')
		{
			fprintf(stderr,"Bad version (%s) in version.cfg",version);
			return 1;
		}
		verMinor = strtoul(end+1,&end,10);
		LogF("Creating patch version %d.%02d",verMajor,verMinor);
	
		// required version
		entry = versionInfo.FindEntry("requiredForPatch");
		if (entry)
		{
			strcpy(version, entry->GetValue());

			char *end;
			if (strtoul(version, &end, 10) != verMajor)
			{
				fprintf(stderr, "Invalid required version %s (major version number need to be %d)", version, verMajor);
			}
			else if (*end != '.')
			{
				fprintf(stderr, "Bad required version format - %s", version);
			}
			else
			{
				verMinorRequired = strtoul(end+1,&end,10);
				LogF("Required version %d.%02d", verMajor, verMinorRequired);
			}
		}
	}

	// copy source exe file to target
	char tgtName[1024];
	char versionName[512];
	if (exePatch || nameByVersion)
	{
		if (patchname)
		{
			sprintf(versionName,"%s_%d.%02d",patchname,verMajor,verMinor);
		}
		else
		{
			sprintf(versionName,"%d.%02d",verMajor,verMinor);
		}
		while (strchr(versionName,'.')) *strchr(versionName,'.')='_';
	}
	else if (patchname)
	{
		strcpy(versionName,patchname);
	}
	else
	{
		strcpy(versionName,patterns[0]);
	}

	HANDLE handle;

	if(!noPack)
	{
		strcpy(tgtName,exePath);
		strcpy(GetFileExt(tgtName),"_");
		strcat(tgtName,versionName);
		strcat(tgtName,".exe");
		LogF("Target file %s",tgtName);
		CopyFile(exePath,tgtName,FALSE);

		handle = BeginUpdateResource(tgtName, FALSE);
		if (!handle)
		{
			DisplayErrorMessage();
			return 1;
		}

		if (patchname && !exePatch) versionInfo.Add("name", patchname);
		if (exePatch) versionInfo.Add("isPatch", exePatch ? 1 : 0);

		QOStrStream out;
		versionInfo.SaveBin(out);
		MemoryToResource(out.str(), out.pcount(), "version.cfg", handle);
	#ifndef _SUPER_RELEASE
		static const unsigned char keyContent[] = {
			0x07, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x32, 0x00, 0x04, 0x00, 0x00,
			0x01, 0x00, 0x01, 0x00, 0x41, 0xB2, 0x6E, 0x5A, 0x85, 0x6B, 0x87, 0x93, 0x8F, 0x8C, 0xB9, 0x0C,
			0x34, 0x2D, 0x47, 0x58, 0xD5, 0x6C, 0x94, 0x33, 0xFA, 0xC2, 0xA1, 0x08, 0x59, 0x3A, 0xD4, 0x44,
			0xED, 0xFB, 0xA2, 0xDC, 0xB0, 0x7A, 0x3A, 0x9C, 0xE5, 0x93, 0x7C, 0xC1, 0x50, 0x49, 0xBB, 0x4D,
			0xED, 0x20, 0x65, 0xAB, 0xF8, 0xFE, 0xAD, 0xB2, 0xBB, 0xE2, 0x00, 0x54, 0x62, 0xB2, 0x96, 0x37,
			0x0E, 0x47, 0xBB, 0x7A, 0x35, 0x9E, 0xD4, 0x85, 0xC4, 0x76, 0x59, 0x92, 0xAF, 0x84, 0xF7, 0x5D,
			0xC1, 0xB3, 0x19, 0xFA, 0x8B, 0xF5, 0xFE, 0x96, 0xB9, 0x05, 0x99, 0xC6, 0x05, 0xC7, 0x8F, 0x6E,
			0x9F, 0x3A, 0xB0, 0xD1, 0x02, 0x3A, 0x8F, 0x3D, 0xA4, 0x08, 0x09, 0x4D, 0xED, 0x88, 0x32, 0x4E,
			0x0E, 0x65, 0xCF, 0x1D, 0xF4, 0xC9, 0xF9, 0x30, 0x56, 0x54, 0xF6, 0x33, 0xF0, 0x85, 0x57, 0x94,
			0x3C, 0xB0, 0x53, 0xCA, 0xB7, 0x70, 0x5B, 0x35, 0x89, 0xFF, 0xA9, 0x2E, 0xA0, 0xC7, 0xC4, 0xCE,
			0xCA, 0x59, 0xC0, 0xE1, 0x60, 0xAD, 0x6E, 0xCA, 0x00, 0x7C, 0x1E, 0x17, 0x8C, 0xEC, 0xD2, 0x1F,
			0x19, 0xF0, 0x30, 0xCF, 0x01, 0x7B, 0x49, 0x94, 0x86, 0xE4, 0x07, 0x82, 0x89, 0x86, 0xEB, 0x83,
			0x45, 0x14, 0x05, 0x8F, 0x9F, 0xAC, 0x96, 0xC8, 0x38, 0x43, 0x65, 0x6D, 0x42, 0x7F, 0x34, 0xA7,
			0xCB, 0x22, 0x1A, 0xE6, 0xC7, 0x8C, 0xFA, 0x94, 0x8F, 0xB0, 0x96, 0x40, 0xDE, 0xDD, 0x73, 0x61,
			0x1C, 0x30, 0x8B, 0x39, 0xAA, 0x17, 0xDB, 0x02, 0xCF, 0x56, 0xE9, 0x51, 0x81, 0x1C, 0x55, 0xA5,
			0xD9, 0x60, 0x46, 0xAD, 0xE1, 0x0B, 0x74, 0x45, 0x5A, 0xE9, 0x4D, 0x04, 0xE5, 0x8C, 0xBF, 0xCB,
			0xBD, 0x44, 0x67, 0x80, 0xD6, 0x65, 0x35, 0x50, 0xC0, 0x5B, 0x79, 0x22, 0x62, 0x45, 0x6A, 0x56,
			0x34, 0x46, 0x19, 0xE1, 0x87, 0x22, 0x7B, 0xD1, 0x87, 0xB1, 0x43, 0x48, 0x0D, 0x90, 0xEF, 0x4D,
			0x42, 0x3F, 0x71, 0x4F, 0x77, 0x06, 0x2C, 0x73, 0x84, 0x41, 0x49, 0x95, 0xB4, 0x0C, 0xE7, 0x93,
			0x8B, 0xEC, 0x6C, 0x22, 0x11, 0x79, 0x30, 0x71, 0xBB, 0xBC, 0xB2, 0x2B, 0xAA, 0x6F, 0x11, 0x75,
			0x62, 0x79, 0x33, 0xFC, 0xA4, 0xC5, 0xCE, 0x22, 0x20, 0xB5, 0x8F, 0xE9, 0x79, 0x77, 0x9F, 0x1E,
			0xF8, 0x49, 0x10, 0x1C, 0xFD, 0x11, 0xEF, 0x45, 0x3C, 0x40, 0xDB, 0x92, 0xA3, 0x66, 0x23, 0x48,
			0x45, 0x41, 0xB3, 0x7C, 0xA3, 0xF5, 0xEF, 0xCF, 0x0D, 0x18, 0xBD, 0xC3, 0x14, 0x17, 0xE0, 0x30,
			0xFE, 0x58, 0x53, 0x7B, 0x32, 0x46, 0x52, 0xF5, 0x66, 0x99, 0xCF, 0x44, 0x72, 0x91, 0x24, 0x70,
			0xCE, 0x44, 0xBB, 0x30, 0x68, 0xE8, 0xDA, 0xDA, 0xB0, 0x62, 0xAC, 0x98, 0x6E, 0x55, 0xDC, 0x21,
			0xAC, 0xA4, 0xBC, 0xB3, 0x93, 0x5B, 0xBC, 0x9C, 0x64, 0xD3, 0x14, 0x1B, 0x11, 0xE5, 0xDF, 0x4B,
			0x4D, 0xA6, 0x60, 0x0F, 0x39, 0x95, 0x9D, 0xD8, 0x63, 0x95, 0xD9, 0x94, 0x7D, 0xD1, 0x24, 0x64,
			0x7C, 0xBA, 0xBD, 0xBE, 0x13, 0xA8, 0x1F, 0xF2, 0xD8, 0x6A, 0x6F, 0xEB, 0x24, 0x6B, 0x17, 0x81,
			0xF9, 0xB2, 0x98, 0x34, 0xBD, 0xC0, 0x17, 0x0C, 0xD4, 0xFF, 0x0A, 0xFD, 0x0D, 0x14, 0x0C, 0x5B,
			0x6E, 0xC6, 0x4B, 0x9F, 0x15, 0x41, 0xFD, 0x05, 0x91, 0x34, 0xC1, 0x9C, 0x00, 0xE6, 0x31, 0x7D,
			0xD7, 0x13, 0x8C, 0xEA, 0x19, 0xAF, 0xDF, 0xFA, 0x85, 0x65, 0xC6, 0x72, 0x69, 0x57, 0x8A, 0x85,
			0xE3, 0x24, 0x2D, 0x5E, 0xC4, 0xCF, 0x9E, 0xFD, 0x10, 0x4F, 0x8C, 0x42, 0xB3, 0x6B, 0xE2, 0x07,
			0x5D, 0xED, 0x64, 0x79, 0x57, 0x80, 0x21, 0x27, 0x4B, 0x89, 0xA3, 0x26, 0xB4, 0x5E, 0xFE, 0xDA,
			0xFA, 0x7C, 0xCE, 0x7D, 0x4E, 0x9C, 0x52, 0x75, 0xC7, 0x99, 0x89, 0xBD, 0xF2, 0x4E, 0x3B, 0x90,
			0x14, 0x64, 0x91, 0xD3, 0x81, 0x23, 0x59, 0x38, 0xEE, 0x25, 0x26, 0xF6, 0xF0, 0x6D, 0x49, 0x8D,
			0x23, 0xDD, 0x1C, 0x3E, 0x45, 0x1F, 0x91, 0x2D, 0xE5, 0x3A, 0x8D, 0x16, 0xB7, 0x6C, 0x39, 0x3E,
			0xDE, 0xFB, 0x63, 0xA6, 0xE7, 0x89, 0xB5, 0x16, 0x73, 0x38, 0x43, 0xD1, 0x42, 0xDE, 0xBA, 0xAF,
			0x0F, 0x07, 0x37, 0x97};
		// create a signature for the config resource
		DSKey privateKey;
		privateKey._content.Realloc(sizeof(keyContent));
		memcpy(privateKey._content.Data(), keyContent, sizeof(keyContent));
		DSSignature signature;
		if (CalculateSignature(signature, out.str(), out.pcount(), privateKey))
		{
			MemoryToResource(signature._content1.Data(), signature._content1.Size(), "version.cfg.sign", handle);
		}
	#endif // _SUPER_RELEASE
		out.close();
	}
#ifndef _SUPER_RELEASE
	// load the list of distributions
	AutoArray<DistributionInfo> distributions;

  ParamEntryPtr multiPatch = versionInfo.FindEntry("multiPatch");
  if (!multiPatch)
	LoadDistributions(versionInfo, distributions);
  else
  {
    for (int i=0; i <multiPatch->GetSize();++i)
    {
      RString name = (*multiPatch)[i];
      DistributionInfo mpDist;
      mpDist.id = name;
      mpDist.parentIndex = -1;
      mpDist.initialVersion = "";
      distributions.Add(mpDist);
      LoadDistributions(versionInfo >> name, distributions);
    }
  }

	if (exePatch && !reuse)
	{
		// clear all incremental update files
		if (clean)
		{
			strcpy(path,exePath);
			//strcpy(GetFilenameExt(path),"?.??-?.??");
			// MSVC 6.00  cannot handle the string above
			const char ext1[] = "?.??";
			strcpy(GetFilenameExt(path),ext1);
			strcat(path,"-");
			strcat(path,ext1);

			_finddata_t info;
			long h = _findfirst(path, &info);
			if (h != -1)
			{
				do
				{
					if ((info.attrib & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

					char subPath[1024];
					strcpy(subPath,path);
					strcpy(GetFilenameExt(subPath),info.name);
					RemoveDirectoryRecursive(subPath);

				}
				while (_findnext(h, &info)==0);
				_findclose(h);
			}
		}

		// create incremental update files
		char lastVerPath[1024];
		bool lastVerPathValid = false;
		for (int minor=verMinorRequired; minor<=verMinor; minor++)
		{
			strcpy(path,exePath);
			sprintf(GetFilenameExt(path),"_%d.%02d",verMajor,minor);

			printf("Creating incremental update %d.%.2d                        \n",verMajor,minor);

			// scan folders for incremental updates
			WIN32_FIND_DATA info;
			HANDLE h = FindFirstFile(path, &info);
			if (h != INVALID_HANDLE_VALUE)
			{
				if (lastVerPathValid)
				{
					do 
					{
						if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
						{
							if (*info.cFileName!='_') continue;
							if (strcmp(info.cFileName, ".") && strcmp(info.cFileName, ".."))
							{
								char subPath[1024];
								strcpy(subPath,path);
								char *mask = GetFilenameExt(subPath);
								strcpy(mask,info.cFileName);

								int headLen = mask-subPath;
								FilesCreateIncUpdate(headLen, path, lastVerPath, handle, distributions);
							}
						}
						strcpy(lastVerPath,path);
					}
					while (FindNextFile(h, &info));
				}
				else
				{
					strcpy(lastVerPath,path);
					lastVerPathValid = true;
				}
				FindClose(h);
			}


		}
	}
#endif // #ifndef _SUPER_RELEASE

	//don't create a resource if noPack option is set
	if(noPack)
		return 0;

	if (exePatch)
	{
		// following folder must be copied for exe patch:
		static const char *folders[]=
		{
			"All", "CRC", "Tmp","Dff", NULL
		};
		for (const char **f = folders; *f; f++)
		{
			patterns.Add(*f);
		}
	}

	printf("Adding files into the resource\n");
	
#ifndef _SUPER_RELEASE
	FilterByDistribution filterByDistribution(distributions);
	IFilter *filter = &filterByDistribution;
#else
	IFilter *filter = NULL;
#endif

	// scan all folders listed in patterns
	for (int i=0; i<patterns.Size(); i++)
	{
		strcpy(path,exePath);

		int headLen = GetFilenameExt(path)-path;

		strcpy(GetFilenameExt(path),patterns[i]);


		_finddata_t info;
		long h = _findfirst(path,&info);
		if (h!=-1)
		{
			do 
			{
				char subPath[1024];
				strcpy(subPath,path);
				char *mask = GetFilenameExt(subPath);
				strcpy(mask,info.name);

				if ((info.attrib & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					FileToResource(headLen,subPath, handle, filter);
					continue;
				}
				if (*info.name=='_') continue;
				if (!strcmp(info.name, ".") || !strcmp(info.name, "..")) continue;

				FilesToResource(headLen,subPath, handle, filter);
			}
			while (_findnext(h, &info)==0);
			
			_findclose(h);
		}

	}

#ifndef _SUPER_RELEASE
	// copy directories with incremental update files into resources
	if (exePatch)
	{
		// create resources for files
		strcpy(path,exePath);
		strcpy(GetFilenameExt(path),"*");

		_finddata_t info;
		long h = _findfirst(path, &info);
		if (h != -1)
		{
			do 
			{
				if ((info.attrib & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
				if (*info.name=='_') continue;
				if (!strcmp(info.name, ".") || !strcmp(info.name, "..")) continue;
				char subPath[1024];
				strcpy(subPath,path);
				char *mask = GetFilenameExt(subPath);
				strcpy(mask,info.name);

				// check if it is incremental update folder
				const char *n = info.name;
				if
					(
					isdigit(n[0]) &&
					'.'==(n[1]) &&
					isdigit(n[2]) &&
					isdigit(n[3]) &&
					'-'==(n[4]) &&
					isdigit(n[5]) &&
					'.'==(n[6]) &&
					isdigit(n[7]) &&
					isdigit(n[8])
				)
				{
					// exclude the incremental patches from incorrect versions
					int verMajorFrom = n[0] - '0';
					if (verMajorFrom != verMajor) continue; // wrong major version
					int verMinorFrom = 10 * (n[2] - '0') + (n[3] - '0');
					if (verMinorFrom < verMinorRequired) continue; // minor version lower than required
					int verMajorTo = n[5] - '0';
					if (verMajorTo != verMajor) continue; // wrong major version
					int verMinorTo = 10 * (n[7] - '0') + (n[8] - '0');
					if (verMinorTo <= verMinorFrom) continue; // target version lower or equal than source
					if (verMinorTo > verMinor) continue; // minor version greater than result

					int headLen = mask-subPath;
					FilesToResource(headLen,subPath, handle, filter);
				}
			}
			while (_findnext(h, &info)==0);
			_findclose(h);
		}
	}
#endif // #ifndef _SUPER_RELEASE

#if USE_DELTA3_BINARY_DIFF
	if (exePatch) FileToResource(0,XDelta3FileName, handle, NULL);
#endif

	BOOL ok = EndUpdateResource(handle, FALSE);

	if (!ok)
	{
		HRESULT hr = GetLastError();
		fprintf(stderr,"Error %x updating resources.\n",hr);
		return 1;

	}
	return 0;
}

#pragma comment(lib,"version")


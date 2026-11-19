//-----------------------------------------------------------------------------
// File: enf_rvfilesystem.cpp
//
// Desc: Virtual filesystem
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "system/enf_timer.h"
#include "system/enf_ifilesystem.h"
#include "system/enf_thread.h"
#include <time.h>
#include <stdio.h>
//#include "enf_unzip.h"
//#include "enf_packfile.h"
//#include <sys/paths.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

namespace enf
{
//-----------------------------------------------------------------------
class PUBLIC_API IFileSystemRV: public IFileSystem
{
	uint m_TLS;
public:
	bool Add(CStr path, uint flags, CStr name, int order);
	bool Remove(CStr name);

	FileError GetLastError()
	{
		FileError err = (FileError)((DWORD)TlsGetValue(m_TLS));
		TlsSetValue(m_TLS, (LPVOID)FILEERR_OK);
		return err;
	}

	void SetLastError(FileError err)
	{
		TlsSetValue(m_TLS, (LPVOID)err);
	}

	~IFileSystemRV()
	{
		TlsFree(m_TLS);
	}

	CachePage* RequestPage(PhysFile* file, FileOffset toRead, FileOffset alignedOffset, uint* pageIndex);
	OverlappedRequest* AllocRequest(CachePage* page, FileOffset alignedOffset);
	void FreeCaches(PhysFile* file);
	FileError WaitForData(PhysFile* owner, CachePage* cpage);
	VFile* InternalSysOpenFile(IFileSystem::FileSystemDef* fsdef, const char* origname, const char* name, uint mode, bool explicitFS, const wchar_t* titleName, size_t reserveSize);
	VFindFile* FindFile(CStr name, SFileDescription& firstfile, uint flags, uint filesystem) override;
	IFileSystemRV(CStr profileName, bool logging);
	void InitProfile(CStr profileName);
	bool Delete(CStr name);
	bool MakeDirectory(CStr name);
	uint SysFileAttributes(CStr fileName) const;
};

//-----------------------------------------------------------------------
class PUBLIC_API PhysFileRV: public PhysFile
{
private:
	friend class IFileSystemRV;

	FILE*		m_Handle;
public:
	void Close();
	FileOffset Write(const void *buffer, FileOffset totalLen);
	bool GetDescription(SFileDescription& desc) const;

	PhysFileRV(IFileSystem* fsys, IFileSystem::FileSystemDef* fsdef, CStr name, FileMode mode, FILE* handle): PhysFile(fsys, fsdef, name, mode), m_Handle(handle)
	{
	}

	IFileSystemRV* GetFS() const 
	{
		return static_cast<IFileSystemRV*>(Owner);
	}

	void SetLength(FileOffset len)
	{
		m_Length = len;
	}
};


//-----------------------------------------------------------------------
void PhysFileRV::Close()
{
	if(m_Handle)
	{
		fclose(m_Handle);
		m_Handle = 0;
	}
}

//-----------------------------------------------------------------------
FileOffset PhysFileRV::Write(const void* buffer, FileOffset totalLen)
{
	size_t written = fwrite(buffer, totalLen, 1, m_Handle);
	return (FileOffset)written;
}

//-----------------------------------------------------------------------
void TimeToSystemTime(SDateTime& dtime, time_t time)
{
	struct tm* tt = localtime(&time);
	dtime.Second = tt->tm_sec;
	dtime.Minute = tt->tm_min;
	dtime.Hour = tt->tm_hour + 1;
	dtime.Day = tt->tm_mday;
	dtime.Month = tt->tm_mon + 1;
	dtime.Year = tt->tm_year + 1900;
	dtime.DayOfWeek = tt->tm_wday + 1;
	dtime.Milliseconds = 0;

}

//-----------------------------------------------------------------------
bool PhysFileRV::GetDescription(SFileDescription& desc) const
{
	char path[MAX_FILENAME_LENGTH];
	
	GetFS()->SysGetFilePath(m_pFSDef, GetName().cstr(), path);
	
	struct stat tagStat;
	int ret = stat(path, &tagStat);
	if(ret != 0)
		return false;

	TimeToSystemTime(desc.LastWriteTime, tagStat.st_mtime);
	TimeToSystemTime(desc.CreationTime, tagStat.st_ctime);
	
	desc.Attributes = 0;
	if(tagStat.st_mode & S_IFDIR)
		desc.Attributes |= FILEATTR_DIRECTORY;

//	if(tagStat.st_mode & S_IRUSR)
	//	desc.Attributes |= FILEATTR_READONLY;

	strcpy(desc.FileName, path);
	return true;
}

//-----------------------------------------------------------------------
void IFileSystemRV::InitProfile(CStr profileName)
{
//	SYS_APP_HOME
}

//-----------------------------------------------------------------------
VFindFile* IFileSystemRV::FindFile(CStr name, SFileDescription& firstfile, uint flags, uint filesystem)
{
	return NULL;
}

//-----------------------------------------------------------------------
bool IFileSystemRV::Delete(CStr name)
{
	return true;
}

//-----------------------------------------------------------------------
uint IFileSystemRV::SysFileAttributes(CStr fileName) const
{
	struct stat tagStat;
	int ret = stat(fileName.cstr(), &tagStat);
	if(ret != 0)
		return FILEATTR_INVALID;

	int attr = 0;
	if(tagStat.st_mode & S_IFDIR)
		attr |= FILEATTR_DIRECTORY;

//	if(tagStat.st_mode & S_IRUSR)
//		attr |= FILEATTR_READONLY;

	return attr;
}

//-----------------------------------------------------------------------
bool IFileSystemRV::Add(CStr path, uint flags, CStr name, int order)
{
char cpath[256];

	if(path.IsEmpty())
		return false;

	//if(path[0] == '.')
		//return false;

	uint len = ConvertToSystemName(cpath, path);

	//strip slash if any
	while(len && cpath[len - 1] == '\\')
	{
		cpath[--len] = 0;
	}

	//avoid duplicities
	for(uint n = 0; n < m_Filesystems.GetCardinality(); n++)
	{
		if(m_Filesystems[n].Data->GetPath().cmpi(cpath) == 0)
			return true;
	}


	uint attr = SysFileAttributes(cpath);
	if(attr == FILEATTR_INVALID)
		return false;

	FileSystemDef* fs = NULL;

	if(SysFileAttributes(cpath) & FILEATTR_DIRECTORY)
	{
		return AddFileSystem(NULL, cpath, flags, name, order);
	}
	else
	{
		enf::VFile* fin = enf::GetFileSystem()->GlobalOpen(path, enf::FILEMODE_READ);
		if (fin != NULL)
		{
			return AddFileSystem(fin, cpath, flags, name, order);
		}
	}

	return false;
}

//-----------------------------------------------------------------------
VFile* IFileSystemRV::InternalSysOpenFile(IFileSystem::FileSystemDef* fsdef, const char* origname, const char* name, uint modes, bool explicitFS, const wchar_t* titleName, size_t reserveSize)
{
	char sname[MAX_FILENAME_LENGTH];

	ConvertToSystemName(sname, name);

	FileMode mode = (FileMode)(modes & FILEMODE_MASK);
	const char* flags;

	switch(mode)
	{
	case FILEMODE_READ:
		flags = "rb";
		break;
	case FILEMODE_WRITE:
		flags = "wb";
		break;
	default:
		enf_assert(!"Unknown file mode");
		break;
	}

	FILE* f = fopen(sname, flags);
	
	if(f == NULL)
	{
		return NULL;
	}

	PhysFileRV* file = ENF_NEW PhysFileRV(this, fsdef, origname, mode, f);
	if(mode != FILEMODE_WRITE)
	{
		struct stat tagStat;
		int ret = stat(sname, &tagStat);
		if(ret == 0)
			file->SetLength((FileOffset)tagStat.st_size);
	}
	return file;
}

//-----------------------------------------------------------------------
void IFileSystemRV::FreeCaches(PhysFile* _file)
{
	CMutexLock lock(m_CacheMutex);
}

//-----------------------------------------------------------------------
FileError IFileSystemRV::WaitForData(PhysFile* owner, CachePage* cpage)
{
	return FILEERR_OK;
}

//-----------------------------------------------------------------------
CachePage* IFileSystemRV::RequestPage(PhysFile* _file, FileOffset toRead, FileOffset alignedOffset, uint* pageIndex)
{
	PhysFileRV* file = static_cast<PhysFileRV*>(_file);

	CachePage* ppage = AllocCachePage(file, alignedOffset, pageIndex);
//	OverlappedRequestWin32* request = (OverlappedRequestWin32*)AllocRequest(ppage, alignedOffset);

#define READ_NB(a) ((a + 2047) & ~2047)

	size_t read;
	fseek(file->m_Handle, alignedOffset, SEEK_SET);
	read = fread(ppage->m_pBuffer, READ_NB(toRead), 1, file->m_Handle);
	return ppage;
}

//-----------------------------------------------------------------------
OverlappedRequest* IFileSystemRV::AllocRequest(CachePage* page, FileOffset alignedOffset)
{
	return NULL;
}

//-----------------------------------------------------------------------
bool IFileSystemRV::MakeDirectory(CStr name)
{
	return true;
}
//-----------------------------------------------------------------------
bool IFileSystemRV::Remove(CStr name)
{
	return true;
}

//-----------------------------------------------------------------------
IFileSystemRV::IFileSystemRV(CStr profileName, bool logging): IFileSystem(profileName, logging)
{
	m_TLS = TlsAlloc();
	SetLastError(FILEERR_OK);
	InitProfile(profileName);
}

//-----------------------------------------------------------------------
FileSystem* CreateFileSystem(CStr profileName, bool logging)
{
	g_pFileSystem = ENF_NEW IFileSystemRV(profileName, logging);
	g_pFileSystem->Add(".\\", 0, "CurrentDir", 1000);

	return g_pFileSystem;
}

}
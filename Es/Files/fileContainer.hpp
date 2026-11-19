#ifdef _MSC_VER
#  pragma once
#endif

#ifndef _ES_FILE_CONTAINER_HPP
#define _ES_FILE_CONTAINER_HPP

#include "Es/Containers/forEach.hpp"
#include "Es/Files/filenames.hpp"
#include "Es/Strings/rString.hpp"
#include "El/Common/perfProf.hpp"
#include <io.h>

///////////////////////////////////////////////////////////////////////////////
//
// Container, item, iterator, traits
//
///////////////////////////////////////////////////////////////////////////////

//! File system item
struct FileItem
{
	RString path;
	RString filename;
	bool directory;
};

//! File system container
struct FileContainer
{
	typedef FileItem DataType;
	RString path;

	FileContainer(RString p)
	{
#ifdef _WIN32
    path = p;
#else
    // we need to convert path to lower case
    unixPath(p.MutableData());
    path = p;
#endif
	}
};

//! File system iterator
struct FileIterator
{
#ifdef _WIN32
	_finddata_t info;
	mutable long h;
	bool valid;
#else
  mutable DIR *dir;
  struct dirent *info;
  bool valid;
#endif

	FileIterator()
	{
    #ifdef _WIN32
      h = -1;
      valid = false;
    #else
      dir = NULL;
      info=NULL;
      valid = false;
    #endif
	}

	~FileIterator()
	{
    #ifdef _WIN32
      if (h != -1) _findclose(h);
    #else
      if (dir!=NULL) closedir(dir);
    #endif
	}

	FileIterator(const FileIterator &src)
	{
    #ifdef _WIN32
      h = src.h;
      info = src.info;
      valid = src.valid;
      src.h = -1;
    #else
      dir = src.dir;
      info = src.info;
      valid = src.valid;
      src.dir = NULL;
    #endif
	}
	void operator =(const FileIterator &src)
	{
    #ifdef _WIN32
      h = src.h;
      info = src.info;
      valid = src.valid;
      src.h = -1;
    #else
      dir = src.dir;
      info = src.info;
      valid = src.valid;
      src.dir = NULL;
    #endif
	}
};

	#define X_findfirst _findfirst

//! Traits that enable traversing file system
struct FileTraits
{
	typedef FileIterator Iterator;
	typedef FileContainer::DataType ItemType;

	static Iterator Start(const FileContainer &c)
	{
	  PROFILE_SCOPE_EX(fiStr,file);
		FileIterator it;
    #ifdef _WIN32
      it.h = X_findfirst(c.path + RString("*"), &it.info);
      it.valid = it.h != -1;
    #else
      it.dir = OpenDir(c.path);
      it.valid = it.dir!=NULL;
      if (!it.valid) return it;
      it.info = readdir(it.dir);
      it.valid = (it.info!=NULL);
    #endif
		return it;
	}
	static void Advance(const FileContainer &c, Iterator &it)
	{
	  PROFILE_SCOPE_EX(fiNxt,file);
    #ifdef _WIN32
      it.valid = _findnext(it.h, &it.info) == 0;
    #else
      it.info = readdir(it.dir);
      it.valid = (it.info!=NULL);
    #endif
	}
	static bool NotEnd(const FileContainer &c, const Iterator &it)
	{
		return it.valid;
	}

	static ItemType Select(const FileContainer &c, const Iterator &it)
	{
		FileItem item;
		item.path = c.path;
    #ifdef _WIN32
      item.filename = it.info.name;
      item.directory = (it.info.attrib & _A_SUBDIR) != 0;
    #else
      item.filename = it.info->d_name;
      struct stat buf;
      RString filepath = item.path + item.filename;
      if (!stat(filepath,&buf))
        item.directory = S_ISDIR(buf.st_mode);
      else
        item.directory = false;
    #endif
		return item;
	}
};

///////////////////////////////////////////////////////////////////////////////
//
// Useful predicates, adapters, functors
//
///////////////////////////////////////////////////////////////////////////////

//! Default filtering
struct PFileDefault
{
	typedef FileItem Argument;
	
	PFileDefault() {}
	bool operator()(const Argument &arg) const
	{
		if (!arg.directory) return true;
		if (arg.filename[0] != '.') return true;
		if (arg.filename[1] == 0) return false;
		if (arg.filename[1] != '.') return true;
		return arg.filename[2] != 0;
	}
};

//! fix for VC 6.0
template <class Pred>
struct PredNothing
{
	typedef FileItem Argument;
	Pred pred;

	PredNothing(Pred p) : pred(p) {}
	bool operator()(const Argument &arg) const
	{
		return pred(arg);
	}
};

// Creating function for PredNothing
template <class Pred>
PredNothing<Pred> PNothing(Pred p) {return PredNothing<Pred>(p);}

//! Functor returning if item is directory
struct PredIsDir
{
	typedef FileItem Argument;

	PredIsDir() {}
	bool operator()(const Argument &arg) const
	{
		return arg.directory;
	}
};

//! Adapter for recursion
template <class Context, class Pred, class Function>
struct AdapterFileRecursion
{
	Pred pred;
	Function func;

	AdapterFileRecursion(Pred p, Function f) : pred(p), func(f) {}
	bool operator()(const FileItem &file, Context &ctx)
	{
		if (file.directory)
		{
			#ifdef _WIN32
			RString path = file.path + file.filename + RString("\\");
      #else
			RString path = file.path + file.filename + RString("/");
      #endif
			return ForSomeFileR(path, pred, func, ctx);
		}
		return func(file, ctx);
	}
};

//! Creating function for recursion adapter
template <class Context, class Pred, class Function>
AdapterFileRecursion<Context, Pred, Function> AFileR(Pred p, Function f)
{return AdapterFileRecursion<Context, Pred, Function>(p, f);}

//! Check pattern mask
inline bool TestPatternMask(const FileItem &arg, const char *mask)
{
	return Matches(arg.filename, mask);
}

///////////////////////////////////////////////////////////////////////////////
//
// Enumeration (nonrecursive)
//
///////////////////////////////////////////////////////////////////////////////

//! traverse file system container
template <class Function, class Context>
bool ForEachFile(RString path, Function fc, Context &ctx)
{
	FileContainer container(path);
	return ForSome<FileTraits>(container, PFileDefault(), fc, ctx);
}

//! traverse file system container with filtering
template <class Pred, class Function, class Context>
bool ForSomeFile(RString path, Pred pred, Function fc, Context &ctx)
{
	FileContainer container(path);
	return ForSome<FileTraits>(container, PAnd(pred, PFileDefault()), fc, ctx);
}

//! traverse file system container with filtering via mask
template <class Function, class Context>
bool ForMaskedFile(RString path, const char *mask, Function fc, Context &ctx)
{
	return ForSomeFile(path, PNothing(AFix2Func2(TestPatternMask, mask)), fc, ctx);
}

///////////////////////////////////////////////////////////////////////////////
//
// Enumeration (recursive)
//
///////////////////////////////////////////////////////////////////////////////

//! traverse file system container recursively
template <class Function, class Context>
bool ForEachFileR(RString path, Function fc, Context &ctx)
{
	FileContainer container(path);
	return ForSome<FileTraits>(container, PFileDefault(), AFileR<Context>(PredTrue<FileItem>(), fc), ctx);
}

//! traverse file system container recursively with filtering
template <class Pred, class Function, class Context>
bool ForSomeFileR(RString path, Pred pred, Function fc, Context &ctx)
{
	FileContainer container(path);
	return ForSome<FileTraits>(container, PAnd(pred, PFileDefault()), AFileR<Context>(pred, fc), ctx);
}

//! traverse file system container recursively with filtering via mask
template <class Function, class Context>
bool ForMaskedFileR(RString path, const char *mask, Function fc, Context &ctx)
{
	return ForSomeFileR(path, POr(PredIsDir(), PNothing(AFix2Func2(TestPatternMask, mask))), fc, ctx);
}

#endif

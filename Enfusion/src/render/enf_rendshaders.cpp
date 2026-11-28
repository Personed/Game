//-----------------------------------------------------------------------------
// File: enf_rendshaders.cpp
//
// Desc: Vertex/Pixel shader abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "enf_main.h"
#include "render/enf_irendmain.h"
#include "render/enf_renderresources.h"
#include "system/enf_vfilesystem.h"
#include "common/enf_streams.h"
#include "engine/enf_engine.h"
#include "common/enf_staticbuffer.h"

/*!
whether to generate shader combinations for offline compiling
*/
#ifdef ENF_DEVELOPER
	//#define GENERATE_SHADERS_OFFLINE
#endif


#if defined(ENF_OGL)
	#include <cg/cgGL.h>
#endif

namespace enf
{
const char	ShaderCacheVersion[] = "00000097";
#define ENF_SHADERSOURCE_DIR "shaders"

#ifdef ENF_OGL
	#define ENF_SHADERCACHE_DIR "shadercacheOGL"
#else
	#define ENF_SHADERCACHE_DIR "shadercache"
#endif

#define ENF_SHADERSOURCE_PATH (ENF_SHADERSOURCE_DIR "/")
#define ENF_SHADERCACHE_PATH (ENF_SHADERCACHE_DIR "/")
#define ENF_SHADERCACHE_VERSION (ENF_SHADERCACHE_DIR "/version")

//-----------------------------------------------------------------------
class OfflineNameCacheDummy
{
public:
	void Finalize() {}
	void AddNames(CStr srcName, CStr destName)	{}
	void AddFlags(uint flags) {}
	void AddProfile(CStr profile) {}
	void AddMacros(const SString<>& macros) {}
	
	void Save() 
	{
		LogF(LL_ERROR, "No offline shaders cache generated, GENERATE_SHADERS_OFFLINE is not enabled");
	}

	bool UseOfflineCache() 
	{ 
		return false; 
	}
};

//-----------------------------------------------------------------------
class OfflineNameCache : public OfflineNameCacheDummy
{
private:
	Array<DString> m_CommandLine;

	SString<>	m_SrcName;
	SString<>	m_DestName;
	SString<>	m_Profile;
	SString<>	m_Defines;
	uint			m_iFlags;

	SString<>	m_DestPath;
	SString<>	m_SrcPath;
public:
	//-----------------------------------------------------------------------
	void AddNames(CStr srcName, CStr destName)
	{
		const char* s = srcName.strrchr('/');
		m_SrcName  = s ? s + 1 : srcName;

		s = destName.strrchr('/');

		if (m_DestPath.IsEmpty())
		{
			//fill m_DestPath with a name which will be later replaced in a batch file
			m_DestPath = " \"OUTPUT_PATH/";

			/*
			//fill m_DestPath with the system shadercache absolute path
			SString<> cdir, path;

			bool ok = GetFileSystem()->GetDirectoryOfFile(destName, (char*) cdir.cstr());
			ok = ok && GetFileSystem()->GetAbsoluteName(cdir.cstr(), (char*) path.cstr(), true);

			if (!ok)
			{
				LogF(LL_NORMAL, "Cannot retrieve directory of shadercache %s", destName.cstr());
				m_DestPath = " ";
			}
			else
			{
				m_DestPath = " \"";
				m_DestPath += path;
				m_DestPath += "\\";
			}
			*/
		}

		m_DestName = m_DestPath;
		m_DestName += s ? s + 1 : destName;
		m_DestName += "\" ";
	}

	//-----------------------------------------------------------------------
	void AddFlags(uint flags) 
	{
		m_iFlags = flags;
	}

	//-----------------------------------------------------------------------
	void AddProfile(CStr profile) 
	{
		m_Profile = profile;
	}

	void AddMacros(const SString<>& macros) 
	{
		m_Defines = macros;
	}

	//-----------------------------------------------------------------------
	void Finalize()
	{
		SString<> res;

		#if defined(ENF_XENON)
			//res  = "fxc ";
			res += "/T";
			res += m_Profile;

			/*
			//do not use it for now, options can be set from batch file
			struct _AvailableFlags
			{ 
				uint Flag; 
				CStr Name; 
			} const AvailableFlags[] =
			{
				{D3DXSHADER_AVOID_FLOW_CONTROL, " /Gfa"},
				{D3DXSHADER_DEBUG, " /Zi"},
				{D3DXSHADER_OPTIMIZATION_LEVEL0, " /O0"},
				{D3DXSHADER_OPTIMIZATION_LEVEL1, " /O1"},
				{D3DXSHADER_OPTIMIZATION_LEVEL2, " /O2"},
				{D3DXSHADER_OPTIMIZATION_LEVEL3, " /O3"},
				{D3DXSHADER_PARTIALPRECISION, " /Gpp"}
			};

			for (uint i = 0; i < sizeof(AvailableFlags)/sizeof(_AvailableFlags) && m_iFlags; i++)
			{
				if (m_iFlags & AvailableFlags[i].Flag)
				{
					res += AvailableFlags[i].Name;
					m_iFlags &= ~AvailableFlags[i].Flag;
				}
			}

			enf_assert2(!m_iFlags, "Unknown flag");
			*/

			res += " ";
			res += m_SrcName;

			res += " /Fo";
			res += m_DestName;

			res += " ";
			res += m_Defines;

		#elif defined(ENF_PS3) || defined(OGL)
			res += "-profile ";
			res += m_Profile;

			res += " ";
			res += m_SrcName;

			res += " -o";
			res += m_DestName;

			res += " ";
			res += m_Defines;

		#else
			enf_assert(0);
		#endif

		//replace all = chars for # chars, the batch compiler cannot correctly process = char
		uint length = res.Length();
		for (uint i = 0; i < length; i++)
		{
			if (res[i] == '=')
				const_cast<char*> (res.cstr())[i] = '#';
		}

		//just test, not neccesary
		m_CommandLine.Insert(DString(res.cstr()));

		m_DestName.Clear();
		m_SrcName.Clear();
	}

	//-----------------------------------------------------------------------
	void Save()
	{
		bool correct = false;

		#if defined(ENF_OGL)
				VFile* f = GetEngine()->GetFileSystem()->Open("shaders/OGL.lst", FILEMODE_WRITE);

			if(f)
			{
				const char endLine[] = {13, 10};
				for (uint i = 0; i < m_CommandLine.GetCardinality(); i++)
				{
					f->Write(m_CommandLine[i].cstr(), m_CommandLine[i].Length());
					f->Write(endLine, 2);
				}
				f->Close();

				correct = true;
			}
			
		#elif defined(ENF_XENON) || defined(ENF_PS3)
			LogF(LL_NORMAL, "-----------------------------------------");
			LogF(LL_NORMAL, "Writing offline shader cache combinations");

			for (uint i = 0; i < m_CommandLine.GetCardinality(); i++)
			{
				LogF(LL_NORMAL, "%s", m_CommandLine[i].cstr());
			}

			correct = true;
		#else
			enf_assert(0);
		#endif


		if (correct)
		{
			LogF(LL_NORMAL, "Offline shaders lst file generated (%d shaders combinations)", m_CommandLine.GetCardinality());
		}
		else
		{
			LogF(LL_ERROR, "Cannot write offline shader cache lst file");
		}
	}

	//-----------------------------------------------------------------------
	bool UseOfflineCache() 
	{ 
		return true; 
	}
};


#ifdef GENERATE_SHADERS_OFFLINE
	static OfflineNameCache OfflineCache;
#else 
	static OfflineNameCacheDummy OfflineCache;
#endif

//-----------------------------------------------------------------------
RenderShader::~RenderShader()
{
	if(GetDevice()->m_Shaders.Remove(this))
	{
		GetDevice()->m_ShadersHash.Remove((const ShaderDef*)this);
	}
}

//-----------------------------------------------------------------------
VertexShader::VertexShader(IRenderer* dev, const ShaderDef* def):	RenderShader(dev, def)
{
	dev->AddResource(this);
}

//-----------------------------------------------------------------------
void VertexShader::Free()
{
	enf_rlsassert(GetDevice()->CheckThread());
#if defined(ENF_OGL)
	if(m_pShader)
	{
		cgGLUnloadProgram((CGprogram)m_pShader);
		cgDestroyProgram((CGprogram)m_pShader);
		m_pShader = NULL;
	}
#endif
}

//-----------------------------------------------------------------------
bool VertexShader::Load(CInputStream& stream)
{
	Free();
	
	enf_rlsassert(GetDevice()->CheckThread());
	if(stream.IsOpen())
	{
		size_t len = stream.GetSize();
		ubyte* ptr = ALLOCA(ubyte, len + 1);

		if(stream.Read(ptr, len) == len)
		{
			#if defined(ENF_OGL)
				ptr[len] = 0;
			
				CGprofile profile = cgGLGetLatestProfile(CG_GL_VERTEX);
				CGprogram prog = cgCreateProgram( GetDevice()->GetCGContext(), 
					CG_OBJECT,
					(const char*)ptr,
					profile,
					NULL,
					NULL);

				if(prog)
				{
					if(cgIsProgramCompiled(prog))
						cgCompileProgram(prog);
					cgGLLoadProgram(prog);
					m_pShader = prog;
					return true;
				}
			#endif
		}
	}

	return false;
}

//-----------------------------------------------------------------------
void PixelShader::Free()
{
	enf_rlsassert(GetDevice()->CheckThread());
#if defined(ENF_OGL)
	if(m_pShader)
	{
		cgGLUnloadProgram((CGprogram)m_pShader);
		cgDestroyProgram((CGprogram)m_pShader);
		m_pShader = NULL;
	}
#endif
}

//-----------------------------------------------------------------------
PixelShader::PixelShader(IRenderer* dev, const ShaderDef* def):	RenderShader(dev, def)
{
	dev->AddResource(this);
}

//-----------------------------------------------------------------------
void ShaderDef::SetName(CStr name)
{
	enf_assert2(strchr(name.cstr(), '.') == NULL, "File extension detected!");
	SString<MAX_FILENAME_LENGTH> temp;
	temp = name.cstr();
	temp.lower();
	m_strName = g_pStatic->AddString(temp.cstr());
}

//-----------------------------------------------------------------------
bool PixelShader::Load(CInputStream& stream)
{
	enf_rlsassert(GetDevice()->CheckThread());
	Free();

	if(stream.IsOpen())
	{
		size_t len = stream.GetSize();

		ubyte* ptr = ALLOCA(ubyte, len + 1);
		if(stream.Read(ptr, len) == len)
		{
			#if defined(ENF_OGL)
				ptr[len] = 0;
				CGprofile profile = cgGLGetLatestProfile(CG_GL_FRAGMENT);
				CGprogram prog = cgCreateProgram( GetDevice()->GetCGContext(), 
					CG_OBJECT,
					(const char*)ptr,
					profile,//CG_PROFILE_UNKNOWN,
					NULL,
					NULL);

				if(prog)
				{
					if(cgIsProgramCompiled(prog))
						cgCompileProgram(prog);
					cgGLLoadProgram(prog);
					m_pShader = prog;
					return true;
				}
			#endif
		}
	}

	return false;
}

#ifdef ENF_SHADER_COMPILER

struct ShaderFile
{
	ENF_DECLARE_SMALLOBJECT(ShaderFile);
	DString	name;
	char*		source;
	size_t	size;
	void*		user;

	ShaderFile(): source(NULL), size(0), user(0) {}
	~ShaderFile()
	{
#if defined(ENF_OGL)
		cgSetCompilerIncludeString((CGcontext)user, name.cstr(), NULL);
#endif
		SAFE_DELETE_LOADFILE(source);
	}
};

Array<ShaderFile*> ShaderFileCache;
HashMap<CStr, ShaderFile*, CStrHashFunc> ShaderFileCacheHash;

typedef Pair<CStr, SDateTime> SourceFile;
Set<SourceFile> FileDates;
FlushableObjectAgent< Set<SourceFile> > FileDatesAgent(&FileDates);

#if defined(ENF_OGL)
const char* g_cgIncludeDir = 0;
void CGIncludeCallback( CGcontext context, const char *filename )
{
	SString<MAX_FILENAME_LENGTH> temp;
	temp = g_cgIncludeDir;
	temp += filename;
	void* ptr = NULL;
	size_t len = 0;

	if(GetFileSystem()->Load(temp.cstr(), &ptr, &len, true))
	{
		ShaderFile* sfile = ENF_NEW ShaderFile;
		sfile->name = temp.cstr();
		sfile->source = (char*)ptr;
		sfile->size = len;
		sfile->user = (void*)context;
		ShaderFileCache.Insert(sfile);
		cgSetCompilerIncludeString( context, filename, (const char *)ptr );
	}
}
#endif
#endif

bool forceRecompile = false;
bool versionChecked = false;

static ebool _initializedCRC32 = false;
static uint crcTable [256];

//-----------------------------------------------------------------------
void InitializeCRC32 ()
{
	int byte;

	for (byte = 0; byte < 256; ++ byte)
	{
		uint crc = byte << 24;
		int offset;

		for(offset = 0; offset < 8; ++ offset)
		{
			if (crc & 0x80000000)
				crc = (crc << 1) ^ 0x04c11db7;
			else
				crc <<= 1;
		}

		crcTable [byte] = crc;
	}

	_initializedCRC32 = 1;
}
	 
//-----------------------------------------------------------------------
uint CRC32(const char* data, size_t size)
{
	 uint crc = 0xFFFFFFFF;
	 
	 if (! _initializedCRC32) InitializeCRC32();

	  const char* dataEnd = data + size;

	  while (data < dataEnd)
	  {
			crc = ((crc << 8) | * data ++) ^ crcTable [crc >> 24];        
	  }

	 return ~crc;
}


//-----------------------------------------------------------------------
	ENF_INLINEFUNC void GetMacrosString(SString<>& deferr, SArray<const char*, 64>& args)
	{
		for (uint i = 0; i < args.GetCardinality() && args[i]; i++)
		{
			deferr += args[i];
			deferr += " ";
		}
	}

//-----------------------------------------------------------------------
bool IRenderer::LoadShader(RenderShader* sh, const ShaderDef& def)
{
#ifndef ENF_DUMMYRENDER

	BeginShaderCompile();

	if(!m_bNoShaderGen && !versionChecked)
	{
		char version[16] = "00000000";

		VFile* vf = GetFileSystem()->Open(ENF_SHADERCACHE_VERSION, FILEMODE_READ);
		if(vf)
		{
			vf->Read(version, 8);
			vf->Close();
		}

		if(memcmp(version, ShaderCacheVersion, 8) != 0)
		{
			LogF(LL_NORMAL, "Shader cache '%s' differs from '%s'. Force recompile...", version, ShaderCacheVersion);
			VFile* vf = GetFileSystem()->Open(ENF_SHADERCACHE_VERSION, FILEMODE_WRITE);
			if(vf)
			{
				vf->Write(ShaderCacheVersion, 8);
				vf->Close();
			}
			forceRecompile = true;
		}
		versionChecked = true;
	}

	//source directory is always the same - "shaders"
	SString<> srcname;
	srcname = ENF_SHADERSOURCE_PATH;
	srcname += def.GetName();
	srcname += ".hlsl";

	if(def.GetDefines().IsEmpty())
	{
		SString<> temp;
		temp = ENF_SHADERSOURCE_PATH;
		temp += def.GetName();
		temp += IsPixelShader(def.GetProfile()) ? ".pso" : ".vso";
	}

	SString<> fullname;

	static const char* shortprofilenames[] = {
		"",
		"$20",
		"$2a",
		"$30",
		"$20",
		"$2a",
		"$30"
	};

	fullname = ENF_SHADERCACHE_PATH;
	fullname += def.GetName();

#ifdef ENF_CONSOLE
	SString<> mang;
	//create mangled output name
	mang = shortprofilenames[def.GetProfile()];

	const DefineSet& dset = def.GetDefines();
	for(uint n = 0; n < dset.GetCardinality(); n++)
	{
		mang += "$";
		mang += dset[n].Name;
		if(!dset[n].Value.IsEmpty())
		{
			mang += "$";
			mang += dset[n].Value;
		}
	}

	uint crc = CRC32(mang.cstr(), mang.Length());
	fullname += mang.format("%08x", crc);

#else
	//create mangled output name
	fullname += shortprofilenames[def.GetProfile()];

	const DefineSet& dset = def.GetDefines();
	for(uint n = 0; n < dset.GetCardinality(); n++)
	{
		fullname += "$";
		fullname += dset[n].Name;
		if(!dset[n].Value.IsEmpty())
		{
			fullname += "$";
			fullname += dset[n].Value;
		}
	}
#endif
	fullname += IsPixelShader(def.GetProfile()) ? ".pso" : ".vso";

	SFileDescription desc;
	bool hasCompiled = false;
	VFile* shaderFile = GetFileSystem()->Open(fullname.cstr(), FILEMODE_READ);
	if(shaderFile && !forceRecompile)
		hasCompiled = shaderFile->GetDescription(desc);

	bool isOld = false;

#ifdef ENF_SHADER_COMPILER
	if(!m_bNoShaderGen)
	{
		SourceFile sfile;
		sfile.Key = def.GetName();
		uint index = FileDates.GetIndexOf(sfile);
		
		if(index == INDEX_NOT_FOUND)
		{
			SFileDescription srcdesc;

			if(GetFileSystem()->GetFileDescription(srcname.cstr(), srcdesc))
			{
				sfile.Data = srcdesc.LastWriteTime;
				FileDates.Insert(sfile, &index);
			}
		}

		isOld = index == INDEX_NOT_FOUND || FileDates[index].Data > desc.LastWriteTime;
	}

	bool mustCompile = !hasCompiled || isOld;
	if(mustCompile || OfflineCache.UseOfflineCache())
	{
		if (mustCompile)
			LogF(LL_NORMAL, "Shader '%s' has newer source '%s'. Recompiling ", fullname.cstr(), srcname.cstr());

		if (OfflineCache.UseOfflineCache())
			OfflineCache.AddNames(srcname.cstr(), fullname.cstr());

		void* ptr = NULL;
		size_t len;
		char cdir[MAX_FILENAME_LENGTH] = "\0";

		bool correctlyLoaded = mustCompile && (GetFileSystem()->GetDirectoryOfFile(srcname.cstr(), cdir) && GetFileSystem()->Load(srcname.cstr(), &ptr, &len));

		if(correctlyLoaded || OfflineCache.UseOfflineCache())
		{
			const DefineSet& dset = sh->GetDefines();
			ProfileType profile = def.GetProfile();

#if defined(ENF_OGL)
			g_cgIncludeDir = cdir;

			cgSetCompilerIncludeCallback(m_cgContext, &CGIncludeCallback );

			static const CGprofile profilenames[] = {
				CG_PROFILE_UNKNOWN,
				CG_PROFILE_VP30,
				CG_PROFILE_VP30,
				CG_PROFILE_GP4VP,
				CG_PROFILE_FP30,
				CG_PROFILE_FP30,
				CG_PROFILE_GP4FP,
			};

			SArray<const char*, 64> args;
			DString define;
			for(uint n = 0; n < dset.GetCardinality(); n++)
			{
				define = "-D";
				define += dset[n].Name.cstr();
				if(!dset[n].Value.IsEmpty())
				{
					define += "=";
					define += dset[n].Value;
				}
				char* buff = ALLOCA(char, define.Length() + 1);
				strcpy(buff, define.c_str());
				args.Insert(buff);
			}

			args.Insert("-DOGL");
			args.Insert("-DCOMPRESSED");
#ifdef __APPLE__
			args.Insert("-D__APPLE__");
#endif
			if(IsPixelShader(profile))
				args.Insert("-D__PS__");
			if(IsVertexShader(profile))
				args.Insert("-D__VS__");

			args.Insert(NULL);

			CGprofile prof = IsPixelShader(profile) ? cgGLGetLatestProfile(CG_GL_FRAGMENT) : cgGLGetLatestProfile(CG_GL_VERTEX);

			CGprogram prog = NULL;

			if (correctlyLoaded)
			{
				prog = cgCreateProgram( m_cgContext,
					CG_SOURCE,
					(const char*)ptr,
					prof,//profilenames[profile],
					NULL,
					args.GetArray());
			}

			SString<> deferr;
			GetMacrosString(deferr, args);

			if(prog != NULL && correctlyLoaded)
			{
				if(!cgIsProgramCompiled(prog))
					cgCompileProgram(prog);
				const char* ptr = cgGetProgramString(prog, CG_COMPILED_PROGRAM );

				//need to close, otherwise write will fail
				if(shaderFile != NULL)
				{
					shaderFile->Close();
					shaderFile = NULL;
				}

				VFile* outf = GetEngine()->GetFileSystem()->Open(fullname.cstr(), FILEMODE_WRITE);
				if(outf)
				{
					LogF(LL_NORMAL, "Recompile OK. Writing variant %s", deferr.cstr());
					outf->Write(ptr, enf::strlen(ptr));
					outf->Close();
				}
				else
				{
					LogF(LL_ERROR, "Recompile OK, but writing failed.");
				}
				cgDestroyProgram(prog);
			}
			else
			{
				if (correctlyLoaded)
					enf::LogF(LL_ERROR, "Can't compile shader '%s' for variant '%s'", srcname.cstr(), deferr.cstr());
			}
#endif
		}

		if (mustCompile && !correctlyLoaded)
		{
			enf::LogF(LL_ERROR, "Can't load shader source '%s'.", srcname.cstr());
		}

		SAFE_DELETE_LOADFILE(ptr);
	}
#endif //#ifdef ENF_SHADER_COMPILER

	if (OfflineCache.UseOfflineCache())
		OfflineCache.Finalize();

	if(shaderFile == NULL)
	{
		shaderFile = GetFileSystem()->Open(fullname.cstr(), FILEMODE_READ);
	}

	bool loaded = false;

	if(shaderFile)
	{
		CFileInputStream fin(shaderFile);
		loaded = sh->Load(fin);
	}

	if(!loaded)
	{
		enf::LogF(LL_ERROR, "Renderer::LoadShader can't load '%s'", fullname.cstr());
	}
	EndShaderCompile();
	return loaded;

#else
	return true;
#endif
}

//-----------------------------------------------------------------------
void IRenderer::BeginShaderCompile()
{
	if(m_iShaderCompile == 0)
	{
		#ifdef ENF_SHADER_COMPILER
			FileDates.Clear();
		#endif
	}
	m_iShaderCompile++;	
}

//-----------------------------------------------------------------------
void IRenderer::EndShaderCompile()
{
	enf_assert(m_iShaderCompile > 0);
	if(--m_iShaderCompile == 0)
	{
#ifdef ENF_SHADER_COMPILER
		//clear file cache
		ForEach<DeleteItem>(ShaderFileCache);
		ShaderFileCacheHash.Clear();
		ShaderFileCache.Clear();

		//clear date cache
		FileDates.Clear();
		FileDates.Flush(ENF_CLEANUP_FLUSHLEVEL);
#endif
	}
}

//-----------------------------------------------------------------------
RenderShader* IRenderer::GetShader(CStr name, ProfileType profile, const Define* defines, uint numdefines)
{
	ShaderDef def(name, profile);

	for(uint n = 0; n < numdefines; n++)
		def.GetDefines().Insert(defines[n]);

	RenderShader* shader = m_ShadersHash.Find(&def);

	if(shader != NULL)
	{
		shader->AddRef();
		return shader;
	}

	RenderShader* sh;
	
	if(IsPixelShader(profile))
		sh = ENF_NEW PixelShader(this, &def);
	else
		sh = ENF_NEW VertexShader(this, &def);

	if(LoadShader(sh, def))
	{
		m_ShadersHash.Insert((const ShaderDef*)sh, sh);
		m_Shaders.Insert(sh);
		sh->AddRef(); //prevent shader from unloading
	}
	else
	{
		SAFE_RELEASE(sh);
	}

	return sh;
}

//-----------------------------------------------------------------------
void IRenderer::ReloadShaders(const char* srcshader)
{
	forceRecompile = false;
	versionChecked = false;

	BeginShaderCompile();

	for(uint n = 0; n < m_Shaders.GetCardinality(); n++)
	{
		RenderShader* shader = m_Shaders[n];
		CStr sname = shader->GetName();
		if(srcshader != NULL && sname.cmpi(srcshader))
			continue;

		bool res = LoadShader(shader, *shader);
		enf_assert(res != false);
	}
	EndShaderCompile();
}

//-----------------------------------------------------------------------
void IRenderer::GenerateOfflineShaders()
{
	OfflineCache.Save();
}


}

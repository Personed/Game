//-----------------------------------------------------------------------------
// File: enf_scriptmain.h
//
// Desc: Main script context, holds and manage compiled image
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_SCRIPTMAIN_H
#define ENF_SCRIPTMAIN_H

#include "enf_config.h"
#include "common/enf_types.h"
#include "script/enf_scripttype.h"
#include "common/enf_hashmap.h"
#include "common/enf_set.h"

namespace enf
{
/** @defgroup EnforceScript Enforce script
@{*/
class StaticBuffer;
class ScriptContext;
class ScriptModule;
class ScriptThread;
class CParser;
struct TToken;
class DebugInfo;
struct Stat;

//!Built-in function pointer declaration
#if defined(__GCC__) && !defined(__APPLE__)
	typedef void (* SCAPI_FUNC)(IVariable **parm,Variable *result);
	typedef void (Instance::*SCAPI_METHOD)(IVariable **parm,Variable *result);
#else
	typedef void (NMETHOD * SCAPI_FUNC)(IVariable **parm,Variable *result);
	typedef void (NMETHOD Instance::*SCAPI_METHOD)(IVariable **parm,Variable *result);
#endif

	//Glue code
#define AddConst(mod, a) mod->AddVariable(#a, mod->GetContext()->m_vtIntType, 0, enf::VSPEC_CONST)->Int = a;
#define AddConst2(mod, name, a) mod->AddVariable(#name, mod->GetContext()->m_vtIntType, 0, enf::VSPEC_CONST)->Int = a;
#define AddConstF(mod, a) mod->AddVariable(#a, mod->GetContext()->m_vtFloatType, 0, enf::VSPEC_CONST)->Float = a;

	//registruje funkci
#define RegFunct( fn, sccont ) sccont ->LinkFunction( #fn, (enf::SCAPI_FUNC)fn )
#define RegFunct2( fn, fns, sccont ) sccont ->LinkFunction( #fn, (enf::SCAPI_FUNC)fns )
#define RegSFunct( fn, sccont ) sccont ->LinkFunction( #fn, fn )
#define RegSFunct2( fn, fns, sccont ) sccont ->LinkFunction( #fn, fns )

	//registruje class metodu
#define RegMethod( clssname, clss, meth, sccont ) \
	{ \
	typedef void (clss ::*FUNC_PTR)(); \
	FUNC_PTR f = (FUNC_PTR)&clss :: meth; \
	sccont ->LinkMethod(#clssname, #meth, *(enf::SCAPI_METHOD*)&f ); \
	}

#define RegMethod2( clssname, clss, methname, meth, sccont ) \
	{ \
	typedef void (clss ::*FUNC_PTR)(); \
	FUNC_PTR f = (FUNC_PTR)&clss :: meth; \
	sccont ->LinkMethod(#clssname, #methname, *(enf::SCAPI_METHOD*)&f ); \
	}

#define RegSMethod( clssname, clss, meth, sccont ) sccont ->LinkMethod(#clssname, #meth, &clss :: meth )
#define RegSMethod2( clssname, clss, methname, meth, sccont ) sccont ->LinkMethod(#clssname, #methname, &clss :: meth )


//!Function flags
enum FunctFlags 
{
	FN_SCRIPT		= BITF(0), ///<Script type, mutually exclusive with FN_NATIVE and FN_INTERN
	FN_NATIVE		= BITF(1), ///<Native C/C++ call, mutually exclusive with FN_SCRIPT and FN_INTERN
	FN_INTERN		= BITF(2), ///<Special C/C++ call, mutually exclusive with FN_SCRIPT and FN_NATIVE
	FN_VOLATILE		= BITF(3), ///<May by reentrant so we must to store local variables
	FN_PRIVATE		= BITF(4), ///<May not be called from script
	FN_EVENT			= BITF(5), ///<Hint for tools that it is used as event method for scripting
	FN_USED			= BITF(6),
	FN_STATIC		= BITF(7),
	FN_TYPE_MASK	= FN_SCRIPT|FN_NATIVE|FN_INTERN
};

//!Script function definition
class PUBLIC_API Funct
{
protected:
	friend class ScriptThread;
	friend class CCompiler;
	friend class ScriptContext;
	friend class ScriptModule;
	friend class ScriptListing;
	friend class Instance;
	uint					Flags;			///<Function type, see enf::FunctFlags
	Variable*			ReturnValue;	///<Result variable
	union										///<Function code
	{
		Stat*			FirstStat;
		SCAPI_FUNC	Func;
		SCAPI_METHOD Method;
	};
	Array<Variable*>	Variables;			///<Variable definitions
	ushort			ParmNum;				///<Parameters num
	ushort			Length;
	//compiler, profiler, debug, runtime only
	ScriptModule*	Module;				///<Owner module
	const char*		Name;					///<Function name
	uint				DebugOffset;		///<offset into debuglines

public:
	ENF_DECLARE_SMALLOBJECT(Funct);
#ifdef ENF_DEVELOPER
	mutable uint			Profile;				///<Profiler related
	mutable TimeStamp		Time;					///<Profiler related
#endif
	bool IsFlagSet(uint flags) const
	{
		return (Flags & flags) != 0;
	}
	const char* GetName() const
	{
		return Name;
	}
	ScriptModule* GetModule() const
	{
		return Module;
	}
	uint GetNumParameters() const
	{
		return ParmNum;
	}
	Variable* GetVariable(uint index) const
	{
		return Variables[index];
	}
	Variable* GetReturnVariable() const
	{
		return ReturnValue;
	}
	const Stat* GetCode() const
	{
		return FirstStat;
	}
	uint AddVariable(Variable* var);
	ENF_INLINE uint GetNumVariables() const
	{
		return Variables.GetCardinality();
	}

	Funct(uint flags, const char* name);
	~Funct();
};


//! Script VM statistics
struct ScriptStats
{
	uint		m_iFoldedConstant;
	uint		m_iNumVariable;
	uint		m_iAllocated;
	uint		m_iNumFunction;
	uint		m_iNumClass;
	uint		m_iExecuted;
	uint		m_iCalls;
	uint		m_iStats;
	uint		m_iNumThreads;
	uint		m_iNumInstances;
	uint		m_iAllocatedMemory;
	ScriptStats();
};

struct ConstantHashFunc
{
	unsigned int operator()(const Variable* var) const
	{
		return (var->GetTypeIndex() << 24) | var->Int;
	}

	int compare(const Variable* var1, const Variable* var2) const;
};

class ScriptProfile;

//!Script module
class PUBLIC_API ScriptModule: public Instance, public RefCountObject
{
friend class CCompiler;
friend class ScriptListing;
friend class ScriptContext;
friend class ScriptThread;

protected:
	const char*			m_Name;
	static Class*		m_pClassPtr;
	Array<Funct*>		m_Functions;
	Array<Variable*>	m_Variables;
	Array<Variable*>	m_ClassPointers;
	Array<VarType*>	m_VarTypes;

	StaticBuffer*		m_pStatic;
	DebugInfo*			m_pDebugInfo;
	ScriptContext*		m_pContext;
	ScriptModule*		m_pParentModule;
	ScriptModule*		m_pChildModule;
	ScriptModule*		m_pSiblingModule;

	//! Reference for class member references
	struct IRef
	{
		Instance**	m_pInst;
		Variable*	m_pVar;
	};

	//! Reference for function calls
	struct CallRef
	{
		ScriptModule*	Module;
		ushort			FunctionIndex;
		ushort			TypeIndex;
		Instance**		pInstance;
	};

	static const uint MAX_REFS = 16384;

	Array<IRef>		m_Refs;
	Array<CallRef>	m_FunctRefs;

	ScriptStats		m_Stats;

	HashMap<CStr, uint, CStrHashFuncCS>	m_FunctionsHash;
	HashMap<CStr, uint, CStrHashFuncCS>	m_VariablesHash;
	HashMap<CStr, uint, CStrHashFuncCS>	m_VarTypesHash;
	HashMap<Variable*, Variable*, ConstantHashFunc> m_ConstantsHash;

	const char* FindString(const char* str) const;

	ScriptModule(const char* name, ScriptModule* parentModule, StaticBuffer* buff);
	~ScriptModule();
	IVariable* __GetVariable(uint index) const;
	IVariable* __GetVariable2(uint index, Variable*& a, Instance**& iptr) const;
	void r_BindInheritedClasses(Array<VarType*>& varTypes, Class* oldAncestor, Class* newAncestor);
	VarType* r_FindTypeInChildren(const char* name) const;

public:
	ENF_DECLARE_MMOBJECT(ScriptModule);
	const char* AddString(const char* str);

	Variable*	FoldConstant(Variable *v, int alloc);

	ScriptModule* GetParentModule() const
	{
		return m_pParentModule;
	}

	ScriptContext* GetContext() const
	{
		return m_pContext;
	}
	CStr GetName() const
	{
		return m_Name;
	}

	ENF_INLINE bool HasModule(const ScriptModule* mod) const
	{
		enf_assert(mod != NULL);
		for(const ScriptModule* m = this; m; m = m->m_pParentModule)
		{
			if(m == mod) return true;
		}
		return false;
	}

	ENF_INLINE IVariable* _GetVariable(const void* a) const
	{
		if((intptr_t)a >= MAX_REFS)
			return (Variable*)a;
		return __GetVariable((uint)a);
	}

	ENF_INLINE IVariable* _GetVariable2(const void* a, Variable*& var) const
	{
		if((intptr_t)a >= MAX_REFS)
		{
			var = (Variable*)a;
			return (IVariable*)a;
		}
		Instance** iptr;
		return __GetVariable2((uint)a, var, iptr);
	}

	ENF_INLINE IVariable* _GetVariable3(const void* a, Variable*& var, Instance**& iptr) const
	{
		if((intptr_t)a >= MAX_REFS)
		{
			iptr = NULL;
			var = (Variable*)a;
			return (IVariable*)a;
		}
		return __GetVariable2((uint)a, var, iptr);
	}

/** \name Types methods
Variable types creating & manipulating
*/
//@{
	VarType* FindType(const char *val, bool inChildren = false) const;
	Class* FindClass(const char *name) const;
	VarType* AddType(const char *name, VType type, int size);
	bool AddClass(Class* cl, uint flags = 0);
	bool BindClass(Class* cl, uint flags = 0);
	
	//!Debugger support. Validates class pointer
	bool IsClass(const Class* cl) const;

	/*!
	Gets array of all types, basic ones and classes
	\return
	Array of types
	*/
	const Array<VarType*>& GetVarTypes() const
	{
		return m_VarTypes;
	}

	ENF_INLINE VarType* GetVarType(uint index) const
	{
		return m_VarTypes[index];
	}
	Instance* New(const char* classname, bool constructor = false, const void* ext = NULL);
	Instance* New(const Class* cl, Variable **parms = NULL);
	Instance* New(const Class* cl, bool constructor = false, const void* ext = NULL);
//@}

/** \name Variable methods
Variables creating & manipulating
*/
//@{
	Variable* AllocVariable();
	Variable* AllocVariable(const VarType *type, int dim, bool alloc = true, ushort spec = 0);

	Variable* CreateClassVariable(const char* name, const char* type, int dim = 1);
	Variable* GetVariable(const char *name) const;
	uint		GetVariableIndex(const char *name) const;
	Variable* AddConstant(VarType *type, const TToken* init, bool optimize = true);
	Variable* AddConstantI(int value);
	Variable* AddConstantF(float value);
	Variable* AddVariable(const char *name, VarType *type, int dim = 1, short spec = 0);
	void AddVariable(Variable* var);

	/*!
	Gets array of global variables
	\return
	Array of global variables
	*/
	const Array<Variable*>& GetGlobalVariables() const
	{
		return m_Variables;
	}
//@}

/** \name Debugger methods
*/
//@{
	DebugInfo* GetDebugInfo() const
	{
		return m_pDebugInfo;
	}
//@}

/** \name Function methods
Global function and class methods creating & manipulating
*/
//@{

	ENF_INLINE Funct* GetFunction(uint index) const
	{
		return m_Functions[index];
	}

	uint GetFunctionIndex(const char *name) const;
	uint AddFunction(Funct* func);

	Funct* GetFunction(const char *name) const;

	Funct* LinkFunction(const char *name, SCAPI_FUNC func);
	Funct* LinkMethod(const char *className, const char* methodName, SCAPI_METHOD method);
	/*!
	Gets array of global functions
	\return
	Array of global functions
	*/
	const Array<Funct*>& GetGlobalFunctions() const
	{
		return m_Functions;
	}
//@}

/** \name Call methods
Function and method calling
*/
//@{
	Variable* _CallVarArgs(const char *name, bool waitable, va_list list);
	Variable* CallFunction(const char *name, ...);
	Variable* CallTFunction(const char *name, ...);
	void TCall(int fn, Instance **inst);
	SCAPI( _Call );
	SCAPI( _CallFunction );

	/*!
	General method for function calling.
	\param thread Thread pointer
	\param func Function index
	\param inst Instance pointer. When NULL, global function is called
	\param cl Class definition pointer. When NULL, class pointer is taken from instance
	*/
	void Execute(ScriptThread* thread, int func, Instance** inst = NULL, const Class* cl = NULL);

	/*!
	Faster method for class-method calling. No validation is done, all parameters must be correct!
	\param thread Thread pointer
	\param func Function index
	\param inst Instance pointer
	\param cl Class definition pointer
	*/
	void IExecute(ScriptThread* thread, int func, Instance* inst, const Class* cl);
//@}

/** \name Profiler methods
Profiler management
*/
//@{
	/*!
	Add profile
	*/
	void Profile(ScriptProfile& prof, uint flags);

	/*!
	Resets profiler
	*/
	void ResetProfile(uint flags);

//@}

	ENF_INLINE StaticBuffer* GetStaticBuffer() const
	{
		return m_pStatic;
	}

	void ResetStats()
	{
		m_Stats.m_iExecuted = 0;
		m_Stats.m_iCalls = 0;
	}

	const ScriptStats* GetStats() const
	{
		return &m_Stats;
	}

	bool CheckLinking(CParser* parser);

	virtual void Cleanup();
	int NMETHOD _Release()
	{
		return Release();
	}
};

enum DebugMode
{
	SDM_NONE = 0,
	SDM_STEP_INTO,
	SDM_STEP_OVER,
	SDM_STEP_OUT,
	SDM_STEP_TO
};

enum ErrorAction
{
	//!No crash, continue execution
	EA_RETRY,
	//!Let it crash
	EA_ABORT,
	//!No crash, ignore further messages
	EA_IGNORE
};

class VMErrorHandler
{
public:
	virtual ErrorAction HandleError(const char* description)=0;
	virtual ~VMErrorHandler() {}
};

class PUBLIC_API SerializeHandler
{
public:
	virtual bool DoSerializeVariable(const Variable* var, uint& serializeType) const=0;
	virtual ~SerializeHandler() {}
};

class VMError {};

//@}

}
#endif //ENF_SCRIPTMAIN_H

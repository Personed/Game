//-----------------------------------------------------------------------------
// File: enf_scriptvm.cpp
//
// Desc: Script Virtual Machine
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "script/enf_scriptcontext.h"
#include "script/enf_scriptinternal.h"
#include "script/enf_scriptthread.h"
#include "common/enf_staticbuffer.h"
#include "enf_main.h"
#include "script/enf_scriptdebuginfo.h"

namespace enf
{
#if defined(ENF_DEVELOPER) && !defined(ENF_CONSOLE)
	//Turn ON to enable debugging services. Otherwise VM is slightly faster
	#define ENF_ENABLE_DEBUGGER
#endif

//Turn ON to detect trashed stack during native function calls.
#ifdef _DEBUG
	#define ENF_GUARD_STACK
#endif

Variable** ScriptContext::m_pParms;

//-----------------------------------------------------------------------
const char ScriptContext::RTE_WrongParameterValue[] = "Wrong parameter value";
const char ScriptContext::RTE_NewClass[] =			"Can't create class instance";
const char ScriptContext::RTE_NullPointerError[] =	"NULL pointer to instance";
const char ScriptContext::RTE_VIndexOutOfRange[] = "Instance variable index out of range";
const char RTE_VTypeMissmatch[] = "Instance variable doesn't match";
const char ScriptContext::RTE_BadInstancePointer[] = "Bad instance pointer";

//-----------------------------------------------------------------------
IVariable* ScriptModule::__GetVariable(uint index) const
{
	const IRef& ref = m_Refs[index];

//this is nonsense, Variable::Module is because of index to types and we can have variable in our module, using type from another!
//	enf_assert(this == ref.m_pVar->Module);

	Instance* inst = *ref.m_pInst;

	if(inst != NULL)
	{
		uint pos = ref.m_pVar->Pos;
#ifdef _DEBUG

		if(pos >= inst->GetClass()->GetNumVariables())
			m_pContext->Error(ScriptContext::RTE_VIndexOutOfRange);

		if(ref.m_pVar->GetName() != inst->GetClass()->GetVariable(pos)->GetName())
		{
			m_pContext->Error(RTE_VTypeMissmatch);
		}
#endif
		return inst->GetIVariable(pos);
	}

	m_pContext->Error(ScriptContext::RTE_NullPointerError);
	return NULL;
}

//-----------------------------------------------------------------------
IVariable* ScriptModule::__GetVariable2(uint index, Variable*& a, Instance**& iptr) const
{
	const IRef& ref = m_Refs[index];

	iptr = ref.m_pInst;
	Instance* inst = *ref.m_pInst;

	if(inst != NULL)
	{
		uint pos = ref.m_pVar->Pos;

#ifdef _DEBUG
		if(pos >= inst->GetClass()->GetNumVariables())
			m_pContext->Error(ScriptContext::RTE_VIndexOutOfRange);

		if(ref.m_pVar->GetName() != inst->GetClass()->GetVariable(pos)->GetName())
		{
			m_pContext->Error(RTE_VTypeMissmatch);
		}
#endif
		a = ref.m_pVar;
		return (Variable*)inst->GetIVariable(pos);
	}

	m_pContext->Error(ScriptContext::RTE_NullPointerError);
	return NULL;
}

//-----------------------------------------------------------------------
ENF_INLINE void _CheckIndex(ScriptContext* context, const Variable* tmpv, uint idx)
{
	if(idx >= tmpv->ArraySize())
	{
		SString<> tmp;
		tmp.format("Index out of range (%d >= %d)", idx, tmpv->ArraySize());
		context->Error(tmp.cstr());
	}
}

//-----------------------------------------------------------------------
#ifdef ENF_ENABLE_DEBUGGER
	#define DEBUG_STEP (m_DebugMode != SDM_NONE && currentThread->m_iStackPos == m_iDebugStackPos || !m_Watch.IsEmpty())
#else
	#define DEBUG_STEP false
#endif

#ifdef ENF_PS3

extern "C"
{
	typedef int (*pppcNFunc)(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc);
	typedef float (*pppcNFuncF)(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc);

	// ppcArgsType is an array containing a byte type (enum argTypes) for each argument.
	enum argTypes
	{
		ppcINTARG = 1,
		ppcFLOATARG = 2
	};

	const uint ppcVECFLAG = BITF(7);

	static ubyte ppcArgsType[MAX_FUNCTION_PARMS + 1 + 1 + 1];
	static uint ppcArgs[MAX_FUNCTION_PARMS + 1 + 1 + 1];

}

extern "C" int ppcNFunc(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc);
extern "C" float ppcNFuncF(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc);


#elif ENF_XENON
// ppcArgsType is an array containing a byte type (enum argTypes) for each argument.
enum argTypes
{
	ppcINTARG = 1,
	ppcFLOATARG = 2
};

const uint ppcVECFLAG = 0;

static ubyte ppcArgsType[MAX_FUNCTION_PARMS + 1 + 1 + 1];
static uint ppcArgs[MAX_FUNCTION_PARMS + 1 + 1 + 1];

//--------------------------------------------------------------------
int __declspec( naked ) ppcNFunc(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc)
{
	__asm
	{
// Prepare args
_ppcFunc:
		// setup stack
		// Read link register
		mflr	r12
		// Stack the link register
		stw	r12,   -8(r1)
		// Move stack pointer
		stwu	r1,  -70h(r1)
		stw	r22, 0h(r1)
		stw	r23, 4h(r1)
		stw	r24, 8h(r1)
		stw	r25, 0ch(r1)
		stw	r26, 10h(r1)
		stw	r27, 14h(r1)
		stw	r28, 18h(r1)

		mr		r26, r4 //pArgs
		mr		r27, r6 //dwFunc

		sub	r0, r0, r0
		// Counting of used/assigned GPR's
		mr		r23, r0
		// Counting of used/assigned Float Registers
		mr		r22, r0

		add	r28, r3, r5
		// Begin loading and stacking registers
		subi	r25, r3, 1

// Fetch the next argument
ppcNextArg:
		// Increment rArgTypePtr
		addi	r25, r25, 1

		//are we at the end?
		cmplw	cr6, r25, r28
		beq	cr6, ppcArgsEnd
		// Get data type
		lbz	r24, 0(r25)
		cmplwi cr6, r24, 2
		beq	cr6, ppcArgIsFloat

// Load and stack integer arguments
ppcArgIsInteger:
		// Get the arg from the stack
		lwz	r11, 0(r26)

		// r23 holds the integer arg count so far
		cmplwi cr6, r23, 0
		beq    cr6, ppcLoadIntReg0
		cmplwi cr6, r23, 1
		beq    cr6, ppcLoadIntReg1
		cmplwi cr6, r23, 2
		beq    cr6, ppcLoadIntReg2
		cmplwi cr6, r23, 3
		beq    cr6, ppcLoadIntReg3
		cmplwi cr6, r23, 4
		beq    cr6, ppcLoadIntReg4
		cmplwi cr6, r23, 5
		beq    cr6, ppcLoadIntReg5
		cmplwi cr6, r23, 6
		beq    cr6, ppcLoadIntReg6
		cmplwi cr6, r23, 7
		beq    cr6, ppcLoadIntReg7

		// no more than 8 parameters
		b      ppcLoadIntRegUpd

ppcLoadIntReg0:
		mr r3, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg1:
		mr r4, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg2:
		mr r5, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg3:
		mr r6, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg4:
		mr r7, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg5:
		mr r8, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg6:
		mr r9, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg7:
		mr r10, r11

ppcLoadIntRegUpd:
		// Increment used int register count
		addi r23, r23, 1
		// Increment rStackPtr
		addi r26, r26, 4
		b ppcNextArg

// Load and stack float arguments
ppcArgIsFloat:
		// Get the arg from the stack
		lfs fr15, 0(r26)

		// r22 holds the float arg count so far
		cmplwi cr6, r22, 0
		beq    cr6, ppcLoadFloatReg0
		cmplwi cr6, r22, 1
		beq    cr6, ppcLoadFloatReg1
		cmplwi cr6, r22, 2
		beq    cr6, ppcLoadFloatReg2
		cmplwi cr6, r22, 3
		beq    cr6, ppcLoadFloatReg3
		cmplwi cr6, r22, 4
		beq    cr6, ppcLoadFloatReg4
		cmplwi cr6, r22, 5
		beq    cr6, ppcLoadFloatReg5
		cmplwi cr6, r22, 6
		beq    cr6, ppcLoadFloatReg6
		cmplwi cr6, r22, 7
		beq    cr6, ppcLoadFloatReg7
		cmplwi cr6, r22, 8
		beq    cr6, ppcLoadFloatReg8
		cmplwi cr6, r22, 9
		beq    cr6, ppcLoadFloatReg9
		cmplwi cr6, r22, 10
		beq    cr6, ppcLoadFloatReg10
		cmplwi cr6, r22, 11
		beq    cr6, ppcLoadFloatReg11
		cmplwi cr6, r22, 12
		beq    cr6, ppcLoadFloatReg12

		// no more than 13 parameters
		b      ppcLoadFloatRegUpd

ppcLoadFloatReg0:
		fmr fr1, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg1:
		fmr fr2, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg2:
		fmr fr3, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg3:
		fmr fr4, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg4:
		fmr fr5, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg5:
		fmr fr6, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg6:
		fmr fr7, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg7:
		fmr fr8, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg8:
		fmr fr9, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg9:
		fmr fr10, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg10:
		fmr fr11, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg11:
		fmr fr12, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg12:
		fmr fr13, fr15

ppcLoadFloatRegUpd:
		// Increment used float register count
		addi r22, r22, 1
		// Increment used int register count - a float reg eats up a GPR
		addi r23, r23, 1
		// Increment rStackPtr
		addi r26, r26, 4
		b ppcNextArg


// Finished
ppcArgsEnd:
		// Call the function
		mtctr r27
		bctrl

		// Function returned

		lwz	r22, 0h(r1)
		lwz	r23, 4h(r1)
		lwz	r24, 8h(r1)
		lwz	r25, 0ch(r1)
		lwz	r26, 10h(r1)
		lwz	r27, 14h(r1)
		lwz	r28, 18h(r1)

		// Restore callers stack
		addi r1, r1, 70h
		// Fetch return link to caller
		lwz  r12, -8(r1)
		mtlr r12

		blr
	}
}

//--------------------------------------------------------------------
float __declspec( naked ) ppcNFuncF(const ubyte* pArgsType, const uint* pArgs, int iStackArgSize, uint dwFunc)
{
	__asm
	{
// Prepare args
_ppcFunc:
		// setup stack
		// Read link register
		mflr	r12
		// Stack the link register
		stw	r12,   -8(r1)
		// Move stack pointer
		stwu	r1,  -70h(r1)
		stw	r22, 0h(r1)
		stw	r23, 4h(r1)
		stw	r24, 8h(r1)
		stw	r25, 0ch(r1)
		stw	r26, 10h(r1)
		stw	r27, 14h(r1)
		stw	r28, 18h(r1)

		mr		r26, r4 //pArgs
		mr		r27, r6 //dwFunc

		sub	r0, r0, r0
		// Counting of used/assigned GPR's
		mr		r23, r0
		// Counting of used/assigned Float Registers
		mr		r22, r0

		add	r28, r3, r5
		// Begin loading and stacking registers
		subi	r25, r3, 1

// Fetch the next argument
ppcNextArg:
		// Increment rArgTypePtr
		addi	r25, r25, 1

		//are we at the end?
		cmplw	cr6, r25, r28
		beq	cr6, ppcArgsEnd
		// Get data type
		lbz	r24, 0(r25)
		cmplwi cr6, r24, 2
		beq	cr6, ppcArgIsFloat

// Load and stack integer arguments
ppcArgIsInteger:
		// Get the arg from the stack
		lwz	r11, 0(r26)

		// r23 holds the integer arg count so far
		cmplwi cr6, r23, 0
		beq    cr6, ppcLoadIntReg0
		cmplwi cr6, r23, 1
		beq    cr6, ppcLoadIntReg1
		cmplwi cr6, r23, 2
		beq    cr6, ppcLoadIntReg2
		cmplwi cr6, r23, 3
		beq    cr6, ppcLoadIntReg3
		cmplwi cr6, r23, 4
		beq    cr6, ppcLoadIntReg4
		cmplwi cr6, r23, 5
		beq    cr6, ppcLoadIntReg5
		cmplwi cr6, r23, 6
		beq    cr6, ppcLoadIntReg6
		cmplwi cr6, r23, 7
		beq    cr6, ppcLoadIntReg7

		// no more than 8 parameters
		b      ppcLoadIntRegUpd

ppcLoadIntReg0:
		mr r3, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg1:
		mr r4, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg2:
		mr r5, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg3:
		mr r6, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg4:
		mr r7, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg5:
		mr r8, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg6:
		mr r9, r11
		b ppcLoadIntRegUpd
ppcLoadIntReg7:
		mr r10, r11

ppcLoadIntRegUpd:
		// Increment used int register count
		addi r23, r23, 1
		// Increment rStackPtr
		addi r26, r26, 4
		b ppcNextArg

// Load and stack float arguments
ppcArgIsFloat:
		// Get the arg from the stack
		lfs fr15, 0(r26)

		// r22 holds the float arg count so far
		cmplwi cr6, r22, 0
		beq    cr6, ppcLoadFloatReg0
		cmplwi cr6, r22, 1
		beq    cr6, ppcLoadFloatReg1
		cmplwi cr6, r22, 2
		beq    cr6, ppcLoadFloatReg2
		cmplwi cr6, r22, 3
		beq    cr6, ppcLoadFloatReg3
		cmplwi cr6, r22, 4
		beq    cr6, ppcLoadFloatReg4
		cmplwi cr6, r22, 5
		beq    cr6, ppcLoadFloatReg5
		cmplwi cr6, r22, 6
		beq    cr6, ppcLoadFloatReg6
		cmplwi cr6, r22, 7
		beq    cr6, ppcLoadFloatReg7
		cmplwi cr6, r22, 8
		beq    cr6, ppcLoadFloatReg8
		cmplwi cr6, r22, 9
		beq    cr6, ppcLoadFloatReg9
		cmplwi cr6, r22, 10
		beq    cr6, ppcLoadFloatReg10
		cmplwi cr6, r22, 11
		beq    cr6, ppcLoadFloatReg11
		cmplwi cr6, r22, 12
		beq    cr6, ppcLoadFloatReg12

		// no more than 13 parameters
		b      ppcLoadFloatRegUpd

ppcLoadFloatReg0:
		fmr fr1, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg1:
		fmr fr2, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg2:
		fmr fr3, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg3:
		fmr fr4, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg4:
		fmr fr5, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg5:
		fmr fr6, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg6:
		fmr fr7, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg7:
		fmr fr8, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg8:
		fmr fr9, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg9:
		fmr fr10, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg10:
		fmr fr11, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg11:
		fmr fr12, fr15
		b ppcLoadFloatRegUpd
ppcLoadFloatReg12:
		fmr fr13, fr15

ppcLoadFloatRegUpd:
		// Increment used float register count
		addi r22, r22, 1
		// Increment used int register count - a float reg eats up a GPR
		addi r23, r23, 1
		// Increment rStackPtr
		addi r26, r26, 4
		b ppcNextArg


// Finished
ppcArgsEnd:
		// Call the function
		mtctr r27
		bctrl

		// Function returned

		lwz	r22, 0h(r1)
		lwz	r23, 4h(r1)
		lwz	r24, 8h(r1)
		lwz	r25, 0ch(r1)
		lwz	r26, 10h(r1)
		lwz	r27, 14h(r1)
		lwz	r28, 18h(r1)

		// Restore callers stack
		addi r1, r1, 70h
		// Fetch return link to caller
		lwz  r12, -8(r1)
		mtlr r12

		blr
	}
}

//--------------------------------------------------------------------
void __declspec( naked ) ppcFunc(Variable** args, Variable* var, void* pfunc)
{
	__asm
	{
// Prepare args
_ppcFunc:
		// setup stack
		// Read link register
		mflr	r12
		// Stack the link register
		stw	r12,   -8(r1)
		// Move stack pointer
		stwu	r1,  -70h(r1)

		// Call the function
		mtctr r5	//pfunc
		bctrl

		// Function returned

		// Restore callers stack
		addi r1, r1, 70h
		// Fetch return link to caller
		lwz  r12, -8(r1)
		mtlr r12

		blr
	}
}
//--------------------------------------------------------------------
void __declspec( naked ) ppcFuncI(Instance* inst, Variable** args, Variable* var, void* pfunc)
{
	__asm
	{
// Prepare args
_ppcFunc:
		// setup stack
		// Read link register
		mflr	r12
		// Stack the link register
		stw	r12,   -8(r1)
		// Move stack pointer
		stwu	r1,  -70h(r1)

		// Call the function
		mtctr r6
		bctrl

		// Function returned

		// Restore callers stack
		addi r1, r1, 70h
		// Fetch return link to caller
		lwz  r12, -8(r1)
		mtlr r12

		blr
	}
}
		
#elif defined(__MACOSX__)

void CallFuncF(void** parms, int numParms, void* fnPtr, float* retPtr)
{
	__asm__
	(
		"movl %1, %%eax\n"
		"andl $3, %%eax\n"
		"movl $4, %%ebx\n"
		"subl	%%eax, %%ebx\n"
		"shll $2, %%ebx\n"
		"movl %%esp, %%edi\n"
		"subl %%ebx, %%esp\n"
		//"subl $8, %%esp\n"
		"movl %0, %%esi;\n"
		"movl %1, %%eax\n"
	".parmloopn:\n"
		"decl	%%eax\n"
		"cmp	$0, %%eax\n"
		"jl	.endloopn\n"

		"movl	(%%esi, %%eax, 4), %%ebx\n"
		"pushl  %%ebx\n" //IVariable.Int
		"jmp	.parmloopn\n"
	".endloopn:\n"
		"call *%2;\n"
		"movl %%edi, %%esp\n"
		"movl %3, %%ebx\n"
		"fstps (%%ebx)\n"
		: 
		: "g" (parms), "r" (numParms), "g" (fnPtr), "g" (retPtr)
		: "eax", "ebx", "edi", "esi"
	);
}

void CallFuncI(void** parms, int numParms, void* fnPtr, void* retPtr)
{
	__asm__
	(
		"movl %1, %%eax\n"
		"andl $3, %%eax\n"
		"movl $4, %%ebx\n"
		"subl	%%eax, %%ebx\n"
		"shll $2, %%ebx\n"
		"movl %%esp, %%edi\n"
		"subl %%ebx, %%esp\n"
		//"subl $8, %%esp\n"
		"movl %0, %%esi;\n"
		"movl %1, %%eax\n"
	".parmloopni:\n"
		"decl	%%eax\n"
		"cmp	$0, %%eax\n"
		"jl	.endloopni\n"

		"movl	(%%esi, %%eax, 4), %%ebx\n"
		"pushl  %%ebx\n" //IVariable.Int
		"jmp	.parmloopni\n"
	".endloopni:\n"
		"call *%2;\n"
		"movl %%edi, %%esp\n"
		"movl %3, %%ebx\n"
		"movl %%eax, (%%ebx)\n"
		:
		: "g" (parms), "r" (numParms), "g" (fnPtr), "g" (retPtr)
		: "eax", "ebx", "edi", "esi"
	);
}	
#endif

//#pragma optimize("", off)

#ifdef _DEBUG
ENF_INLINE void CheckTypeConversion(const IVariable* iv1, const Variable* ov1, const Variable* ov2)
{
	if(ov1->GetVType() == VARTYPE_CLASS)
	{
		//registers don't have reliable type
		if(ov2->GetName() && ov2->GetName()[0] == '@')
			return;
		//not solved
		if(ov2->IsPtr() || ov1->IsPtr())
			return;

		VType vtype2 = ov2->GetVType();
		if(vtype2 != VARTYPE_CLASS && vtype2 != VARTYPE_VOID)
		{
			ScriptContext::m_pCurrent->Error("Bad type conversion");
		}

		if(iv1->InstancePtr)
		{
			//output parameters don't need to be checked for validity
			if((ov2->Spec & (VSPEC_OUT|VSPEC_INOUT)) != VSPEC_OUT)
			{
				CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
				if(!ScriptContext::m_pCurrent->m_Instances.Contains(iv1->InstancePtr))
					ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
			}

			if(vtype2 == VARTYPE_CLASS)
			{
				const Class* v2Type = (Class*)ov2->GetType();
				if(v2Type == ScriptContext::m_pCurrent->m_vtClassType)
					return; //everyone is descendant of Eva

				const Class* v1Type = iv1->InstancePtr->GetClass();
				if(!v1Type->IsInherited(v2Type))
					ScriptContext::m_pCurrent->Error("Bad type conversion");
			}
		} 
	}
}

#else
	#define CheckTypeConversion(iv1, ov1, ov2)
#endif

ENF_INLINE void TranslateParameters(const ScriptModule* cmod, const Funct* fn, Variable** sparms, Variable** parms)
{
	for(uint np = 0; np < fn->GetNumParameters(); np++)
	{
#ifdef _DEBUG
		Variable* ov1;
		IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov1);
		//don't check variables that are output only
		if((ov1->Spec & (VSPEC_OUT|VSPEC_INOUT)) != VSPEC_OUT)
		{
			CheckTypeConversion(iv1, ov1, fn->GetVariable(np));
		}
		parms[np] = (Variable*)iv1;
#else
		parms[np] = (Variable*)cmod->_GetVariable(sparms[np]);
#endif
	}
}

ENF_INLINE void ValidateParameters(const ScriptModule* cmod, const Funct* fn, Variable** sparms)
{
#ifdef _DEBUG
	for(uint np = 0; np < fn->GetNumParameters(); np++)
	{
		Variable* ov1;
		IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov1);
		//don't check variables that are output only
		if((ov1->Spec & (VSPEC_OUT|VSPEC_INOUT)) != VSPEC_OUT)
			continue;
		CheckTypeConversion(iv1, ov1, fn->GetVariable(np));
	}
#endif
}

//-----------------------------------------------------------------------
void ScriptContext::Run(ScriptThread* currentThread, VMStack* stack, int bstack)
{
	enf_assert(currentThread != NULL);
	enf_assert(stack != NULL);

#ifdef ENF_DEVELOPER
	class RecursionCounter
	{
		int& counter;
	public:
		RecursionCounter(int& c): counter(c) { c++; }
		~RecursionCounter() {counter--;}
	};

	if(m_iRecursion > 256)
	{
		Error("Recursion > 256. Infinite loop?");
	}

	RecursionCounter rec(m_iRecursion);
#endif

char sr;
void* pesp = NULL;

#if defined(ENF_EXCEPTION) //&& !defined(_DEBUG)
	while(true)
	{
	try {
#endif

	//just a sanity check
	enf_assert(NullString[0] == 0);

	if(stack->m_peInstance)
	{
		SetThis(stack->m_peInstance);
	}

	while(true)
	{
		m_Stats.m_iExecuted++;

#if defined(ENF_ENABLE_DEBUGGER) && !defined(ENF_CONSOLE)
		if(m_Stats.m_iExecuted > 1048576) //1MB instructions
		{
			m_Stats.m_iExecuted = 0;
			if(_GetAsyncKeyState(SysKey::Fn + 2) & 0x80000000)
			{
				if(StartDebugger(currentThread->m_sStack[currentThread->m_iStackPos].m_pfFunc, m_pvThis, currentThread->m_sStack[currentThread->m_iStackPos].GetDebugLine()) != 1)
				{
					Error("Deadlock");
				}
				//else
				//	m_bEnableDebug = true;
			}
		}
#endif
		const Stat* ENF_RESTRICT pstat = stack->m_psStat;

#ifdef ENF_ENABLE_DEBUGGER
		if(m_bEnableDebug)
		{
			if(m_DebugMode != SDM_NONE && m_DebugMode != SDM_STEP_TO)
			{
				const DebugInfo* di = stack->m_pfFunc->GetModule()->GetDebugInfo();
				uint dindex = di->GetExactLineByOffset(stack->GetDebugOffset());
				if(dindex != -1)
				{
					if(m_DebugMode == SDM_STEP_OUT)
					{
						//step out. Must equal
						if(m_iDebugStackPos != currentThread->m_iStackPos)
							goto skipDebug1;
					}
					else if(m_DebugMode == SDM_STEP_OVER)
					{
						if(currentThread->m_iStackPos > m_iDebugStackPos || currentThread != m_pLastDebugThread)
						{
							m_bFlushDebugVars = true;
							goto skipDebug1;
						}
					}

					StartDebugger(currentThread->m_sStack[currentThread->m_iStackPos].m_pfFunc, m_pvThis, currentThread->m_sStack[currentThread->m_iStackPos].GetDebugLine());
	skipDebug1:;
				}
			}

			if(!m_Breapoints.IsEmpty())
			{
				uint index = m_Breapoints.GetIndexOf(Breakpoint(pstat, 0));
				if(index != INDEX_NOT_FOUND)
				{
					if(m_DebugMode == SDM_STEP_TO)
					{
						int offset = m_Breapoints[index].Data;
						if(offset & DBGF_TOCURSOR)
							RemoveBreakpoint(pstat);
					}

					StartDebugger(stack->m_pfFunc, m_pvThis, stack->GetDebugLine());
				}
			}
		}
#endif

		uint xcmd = pstat->XCmd;
		OpCode cmd = (OpCode)XCMD_OPCODE(xcmd);

		if(cmd < OP_PARM0)
		{
			switch(cmd)
			{
			case (OP_NCALL):
			{
				int off = XCMD_OFFSET(xcmd);
				ScriptModule* cmod = stack->m_pfFunc->Module;
				const ScriptModule::CallRef& ref = cmod->m_FunctRefs[off & 0x1fff];

				Variable** sparms = m_pParms = (Variable **)pstat->Op;

				Funct* fn = ref.Module->GetFunction(ref.FunctionIndex - 1);

				enf_assert(fn);
				enf_assert2(fn->Func, "Function not linked");
				//only when debugging
#ifdef ENF_ENABLE_DEBUGGER
				if(m_DebugMode != SDM_NONE && (currentThread->m_iStackPos - 1) == m_iDebugStackPos)
				{
					for(uint np = 0; np < fn->ParmNum; np++)
					{
						Variable* ov;
						IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov);
						bool modify = (ov->Spec & (VSPEC_OUT|VSPEC_INOUT)) != 0;
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)sparms[np], modify);
					}
				}
#endif
				ValidateParameters(cmod, fn, sparms);
				//store this pointer
				Instance* inst = m_pvThis->InstancePtr;

#if defined(ENF_XENON) || defined(ENF_PS3)
				{
					uint parm = 0;
					VType rtype = fn->ReturnValue->GetVType();
					if(rtype == VARTYPE_VECTOR)
					{
						ppcArgsType[parm] = ppcINTARG | ppcVECFLAG;
						ppcArgs[parm] = fn->ReturnValue->Int;
						parm++;
					}
					
					for(uint n = 0; n < fn->ParmNum; n++)
					{
						if(fn->Variables[n]->GetVType() == VARTYPE_FLOAT)
							ppcArgsType[parm] = ppcFLOATARG;
						else
							ppcArgsType[parm] = ppcINTARG;
						ppcArgs[parm] = sparms[n]->Int;
						parm++;
					}
					if(rtype == VARTYPE_FLOAT)
						fn->ReturnValue->Float = ppcNFuncF(ppcArgsType, ppcArgs, parm, (uint)fn->Func);
					else
						fn->ReturnValue->Int = ppcNFunc(ppcArgsType, ppcArgs, parm, (uint)fn->Func);
				}
				
#elif defined(__MACOSX__)

				void* fnptr = (void*)fn->Func;

				void* parmArray[16];
				int parmNum = 0;
#define PUSH_PTR(ptr) parmArray[parmNum++]=(void*)ptr;
				
				if(fn->ReturnValue->GetVType() == VARTYPE_VECTOR)
					PUSH_PTR(fn->ReturnValue->Pointer);
				for(uint i = 0; i < fn->ParmNum; i++)
					PUSH_PTR(sparms[i]->Pointer);

				if(fn->ReturnValue->GetVType() == VARTYPE_FLOAT)
					CallFuncF(parmArray, parmNum, fnptr, &fn->ReturnValue->Float);
				else
					CallFuncI(parmArray, parmNum, fnptr, &fn->ReturnValue->Pointer);

#elif defined(WIN32)
				__asm
				{
#ifdef ENF_GUARD_STACK
					mov	edx, [stack]
					mov	dword ptr[edx]VMStack.m_pStackPointer, esp
#endif

					mov   edx, fn
					movsx eax, word ptr [edx]Funct.ParmNum
					mov	esi, sparms
parmloopn:
					dec	eax
					cmp	eax, 0
					jl		short endloopn

					mov	ecx, dword ptr [esi + eax * 4]
					push  dword ptr [ecx]IVariable.Int
					jmp	short parmloopn
endloopn:
					mov	ecx, dword ptr [edx]Funct.ReturnValue

					movsx	eax,dword ptr[ecx]Variable.TypeType
					cmp	eax, VARTYPE_VECTOR
					jne	short novector

					push	dword ptr[ecx]IVariable.Pointer				//return vector
					call	dword ptr [edx]Funct.Func
					jmp	short nofloatn
novector:
					call	dword ptr [edx]Funct.Func

					mov   edx, fn
					mov	ecx, dword ptr [edx]Funct.ReturnValue
					mov	dword ptr[ecx]IVariable.Int, eax				//return int

					movsx	edx, word ptr[ecx]Variable.TypeType
					cmp	edx, VARTYPE_FLOAT
					jne	nofloatn
					fstp	dword ptr[ecx]IVariable.Float					//return float
nofloatn:

#ifdef ENF_GUARD_STACK
					mov	dword ptr[pesp], esp
#endif
				}
#endif

#if defined(ENF_GUARD_STACK) && defined(WIN32)
				if(pesp != stack->m_pStackPointer)
					Error("Stack trashed");
#endif
				SetThis(inst);
			}
			break;

			case (OP_NCALLI):
			{
				int off = XCMD_OFFSET(xcmd);
				ScriptModule* cmod = stack->m_pfFunc->Module;
				const ScriptModule::CallRef& ref = cmod->m_FunctRefs[off & 0x1fff];

				uint func = ref.FunctionIndex - 1;

				Variable **sparms = m_pParms = (Variable **)pstat->Op;

				Instance* inst = *ref.pInstance;
				if(inst == NULL)
					Error(RTE_NullPointerError);
#ifdef _DEBUG
				{
					CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
					if(!m_Instances.Contains(inst))
						Error(RTE_BadInstancePointer);
				}
#endif

				const Class* cl = inst->GetClass();

				enf_assert(cl);

				if(func >= cl->GetNumMethods())
				{
					SString<> tmp;
					tmp.format("Calling nonexistent function on class '%s'", cl->Name);
					Error(tmp.cstr());
				}

				Funct* fn = cl->GetMethod(func);

				enf_assert(fn);
				enf_assert2(fn->Func, "Function not linked");

#ifdef ENF_ENABLE_DEBUGGER
				//only when debugging
				if(m_DebugMode != SDM_NONE && (currentThread->m_iStackPos - 1) == m_iDebugStackPos)
				{
					for(uint np = 0; np < fn->ParmNum; np++)
					{
						Variable* ov;
						IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov);
						bool modify = (ov->Spec & (VSPEC_OUT|VSPEC_INOUT)) != 0;
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)sparms[np], modify);
					}
				}
#endif
				enf_assert((off & 0x8000) == 0);

				Instance* pinst = m_pvThis->InstancePtr;

				ValidateParameters(cmod, fn, sparms);

#if defined(ENF_XENON) || defined(ENF_PS3)
				{
					uint parm = 0;
					VType rtype = fn->ReturnValue->GetVType();
					//when returning vector, push pointer for that
					if(rtype == VARTYPE_VECTOR)
					{
						ppcArgsType[parm] = ppcINTARG | ppcVECFLAG;
						ppcArgs[parm] = fn->ReturnValue->Int;
						parm++;
					}
					//push 'this' pointer
					ppcArgsType[parm] = ppcINTARG;
					ppcArgs[parm] = (int)inst;
					parm++;
					
					for(uint n = 0; n < fn->ParmNum; n++)
					{
						if(fn->Variables[n]->GetVType() == VARTYPE_FLOAT)
							ppcArgsType[parm] = ppcFLOATARG;
						else
							ppcArgsType[parm] = ppcINTARG;
						ppcArgs[parm] = sparms[n]->Int;
						parm++;
					}

					void* fnptr = (void*)fn->Func;
#ifdef ENF_PS3
					//is it virtual method?
					if((uint)fnptr & 1)
					{
						char* vptr = *(char**)inst;
						vptr += (int)fnptr;
						vptr--;
						fnptr = *(void**)vptr;
//						LogF(LL_NORMAL, "%s %x", fn->GetName(), fnptr);
					}
#endif

					if(rtype == VARTYPE_FLOAT)
						fn->ReturnValue->Float = ppcNFuncF(ppcArgsType, ppcArgs, parm, (uint)fnptr);
					else
						fn->ReturnValue->Int = ppcNFunc(ppcArgsType, ppcArgs, parm, (uint)fnptr);
				}
#elif defined(__MACOSX__)

				void* fnptr = (void*)fn->Func;
				//is it virtual method?
				if((uint)fnptr & 1)
				{
					char* vptr = *(char**)inst;
					vptr += (int)fnptr;
					vptr--;
					fnptr = *(void**)vptr;
				}

				void* parmArray[16];
				int parmNum = 0;
				
				if(fn->ReturnValue->GetVType() == VARTYPE_VECTOR)
					PUSH_PTR(fn->ReturnValue->Pointer);
				PUSH_PTR(inst);
				for(uint i = 0; i < fn->ParmNum; i++)
				{
					PUSH_PTR(sparms[i]->Pointer);
				}
				if(fn->ReturnValue->GetVType() == VARTYPE_FLOAT)
					CallFuncF(parmArray, parmNum, fnptr, &fn->ReturnValue->Float);
				else
					CallFuncI(parmArray, parmNum, fnptr, (void*)&fn->ReturnValue->Pointer);

#elif defined(WIN32)
				__asm
				{
#ifdef ENF_GUARD_STACK
					mov	edx, [stack]
					mov	dword ptr[edx]VMStack.m_pStackPointer, esp
#endif
					mov   edx, fn
					movsx eax, word ptr [edx]Funct.ParmNum
					mov	esi, sparms
parmloopni:
					dec	eax
					cmp	eax, 0
					jl		short endloopni

					mov	ecx, dword ptr [esi + eax * 4]
					push  dword ptr [ecx]IVariable.Int
					jmp	short parmloopni
endloopni:
					mov	ecx, dword ptr [edx]Funct.ReturnValue

					movsx	eax, word ptr[ecx]Variable.TypeType
					cmp	eax, VARTYPE_VECTOR
					jne	short novectorni

					push	dword ptr[ecx]IVariable.Pointer				//return vector
					mov	eax, inst
					push	eax
					call	dword ptr [edx]Funct.Func
					jmp	short nofloatni

novectorni:
					//it's class method, pass 'this' pointer
					mov	eax, inst
					push	eax

					call	dword ptr [edx]Funct.Func

					mov   edx, fn
					mov	ecx, dword ptr [edx]Funct.ReturnValue
					mov	dword ptr[ecx]IVariable.Int, eax				//return int

					movsx	edx, word ptr[ecx]Variable.TypeType
					cmp	edx, VARTYPE_FLOAT
					jne	nofloatni
					fstp	dword ptr[ecx]IVariable.Float					//return float
nofloatni:
#ifdef ENF_GUARD_STACK
					mov	dword ptr[pesp], esp
				}
				if(pesp != stack->m_pStackPointer)
					Error("Stack trashed");
#else
				}
#endif
#endif

				SetThis(pinst);
			}
			break;

			case OP_ICALL:
			{
				int off = XCMD_OFFSET(xcmd);

				ScriptModule* cmod = stack->m_pfFunc->Module;
				const ScriptModule::CallRef& ref = cmod->m_FunctRefs[off & 0x1fff];
				Variable** sparms = (Variable**)pstat->Op;

				m_pParms = sparms;
				Variable* parms[MAX_FUNCTION_PARMS];

				if(off & 0x2000)
				{
					Funct* fn = ref.Module->GetFunction(ref.FunctionIndex - 1);

					enf_assert(fn);
					enf_assert2(fn->Func, "Function not linked");
					enf_assert(fn->ParmNum < MAX_FUNCTION_PARMS);

#ifdef ENF_ENABLE_DEBUGGER
					//only when debugging
					if(m_DebugMode != SDM_NONE && currentThread->m_iStackPos == (m_iDebugStackPos & 0xfffffff))
					{
						for(uint np = 0; np < fn->ParmNum; np++)
						{
							Variable* ov;
							IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov);
							bool modify = (ov->Spec & (VSPEC_OUT|VSPEC_INOUT)) != 0;
							TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)sparms[np], modify);
						}
					}
#endif
					if(off & 0x8000)
					{
						TranslateParameters(cmod, fn, sparms, parms);
						sparms = parms;
					}
					else
					{
						ValidateParameters(cmod, fn, sparms);
					}
					//'this' must be saved here, because VM is reentrant even from API functions
					Instance *inst = m_pvThis->InstancePtr;

					fn->Func((IVariable**)sparms, fn->ReturnValue);

					SetThis(inst);
					break;
				}
				
				stack = PushCall(ref.Module, currentThread, ref.pInstance, NULL, ref.FunctionIndex);

				const Funct* fn = stack->m_pfFunc;
#ifdef ENF_ENABLE_DEBUGGER
				//only when debugging
				if(m_DebugMode != SDM_NONE && ((currentThread->m_iStackPos - 1) == (m_iDebugStackPos & 0xfffffff)))
				{
					for(uint np = 0; np < fn->ParmNum; np++)
					{
						Variable* ov;
						IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov);
						bool modify = (ov->Spec & (VSPEC_OUT|VSPEC_INOUT)) != 0;
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)sparms[np], modify);
					}
				}
#endif
				//if some parameters need to translate
				if(off & 0x8000)
				{
					TranslateParameters(cmod, fn, sparms, parms);
					sparms = parms;
				}
				else
				{
					ValidateParameters(cmod, fn, sparms);
				}


				if(stack->m_peInstance)
					(stack->m_peInstance->*fn->Method)((IVariable**)sparms, fn->ReturnValue);
				else
					fn->Func((IVariable**)sparms, fn->ReturnValue);

				stack = PopCall(currentThread);
			}
			break;

			case OP_TCALL:
			{
				int off = XCMD_OFFSET(xcmd);

				const ScriptModule* cmod = stack->m_pfFunc->Module;
				const ScriptModule::CallRef& ref = cmod->m_FunctRefs[off & 0x1fff];

				ScriptThread* rt = CreateThread(true);

				int bstack = rt->m_iStackPos;
				int sreg = (int)pstat->Op[0];
				Variable** sparms = (Variable**)&pstat->Op[1];

				Instance* prevThis = m_pvThis->InstancePtr;

				VMStack* tstack = PushCall(ref.Module, rt, ref.pInstance, NULL, ref.FunctionIndex);

				Variable* parms[MAX_FUNCTION_PARMS];

				//if some parameters need to translate
				TranslateParameters(cmod, tstack->m_pfFunc, sparms, parms);
				sparms = parms;

					//copy parameters
				_CopyParameters(sparms, tstack->m_pfFunc);
					Instance *inst = m_pvThis->InstancePtr;

				Run(rt, tstack, bstack);

				//thread is sleeping - add to user list
				if(!QuitThread())
					StoreThread(tstack->m_peInstance, rt);

				//if(tstack->m_peInstance)
				//	SetThis(tstack->m_peInstance);
				SetThis(prevThis);
			}
			break;

			case OP_CALL:
			{
				int off = XCMD_OFFSET(xcmd);

				ScriptModule* cmod = stack->m_pfFunc->Module;
				const ScriptModule::CallRef& ref = cmod->m_FunctRefs[off & 0x1fff];

				int sreg = (int)pstat->Op[0];
				Variable** sparms = (Variable**)&pstat->Op[1];

				m_pParms = sparms;
				Variable* parms[MAX_FUNCTION_PARMS];
		
				stack = PushCall(ref.Module, currentThread, ref.pInstance, NULL, ref.FunctionIndex);

#ifdef ENF_ENABLE_DEBUGGER
				//only when debugging
				if(m_DebugMode != SDM_NONE && ((currentThread->m_iStackPos - 1) == (m_iDebugStackPos & 0xfffffff)))
				{
					for(uint np = 0; np < stack->m_pfFunc->ParmNum; np++)
					{
						Variable* ov;
						IVariable* iv1 = cmod->_GetVariable2(sparms[np], ov);
						bool modify = (ov->Spec & (VSPEC_OUT|VSPEC_INOUT)) != 0;
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)sparms[np], modify);
					}
				}
#endif
				//if some parameters need to translate
				if(off & 0x8000)
				{
					TranslateParameters(cmod, stack->m_pfFunc, sparms, parms);
					sparms = parms;
				}
				else
				{
					ValidateParameters(cmod, stack->m_pfFunc, sparms);
				}

				/*const char *mod;
				uint sline;
				this->m_pDebugInfo->GetLineInfo(thread->m_sStack[thread->m_iStackPos-1].GetDebugLine(), &mod, &sline);

				LogF(LL_NORMAL, "Func %s, Line %d, File %s", stack->m_pfFunc->Name, sline, mod);
*/
				//store registers
				stack->m_iStored = sreg;
				if(sreg)
				{
					//int registers
					for(int c = 0; c < (sreg & 0xf); c++)
					{
						currentThread->PushData(VARTYPE_INT);
						currentThread->PushData(m_vRegs[c].Int);
					}
					sreg >>= 4;

					//float registers
					for(int c = 0; c < (sreg & 0xf); c++)
					{
						currentThread->PushData(VARTYPE_FLOAT);
						currentThread->PushData(m_vFRegs[c].Int);
					}
					sreg >>= 4;

					//class registers
					for(int c = 0; c < (sreg & 0xf); c++)
					{
						currentThread->PushData(VARTYPE_CLASS);
						currentThread->PushData(m_vCRegs[c].Int);
					}
					sreg >>= 4;

					//string registers
					for(int c = 0; c < (sreg & 0xf); c++)
					{
						currentThread->PushData(VARTYPE_STRING);
						if(IsStatic(m_vSRegs[c].Pointer))
							currentThread->PushData(m_vSRegs[c].Int);
						else
							currentThread->PushData((int)AllocString(strlen((char *)m_vSRegs[c].Pointer) + 1, m_vSRegs[c].Pointer));
					}
					sreg >>= 4;

					//vector registers
					for(int c = 0; c < (sreg & 0xf); c++)
					{
						currentThread->PushData(VARTYPE_VECTOR);
						int *fp = (int *)m_vVRegs[c].Pointer;
						enf_assert(fp != NULL);
						currentThread->PushData(fp[0]);
						currentThread->PushData(fp[1]);
						currentThread->PushData(fp[2]);
					}
				}
				//copy parameters
				if(off & 0x4000)
					_CopyParameters(sparms, stack->m_pfFunc);

				if(stack->m_peInstance)
					SetThis(stack->m_peInstance);
				continue;
			}
			break;

			case (OP_WAIT):
				if(currentThread->m_sState == ScriptThread::TF_NONWAITABLE)
				{
					Error("Cannot stop main thread");
				}

				if(currentThread->m_sState != ScriptThread::TF_KILL)
				{
					if(pstat->Op[0])
					{
						const IVariable* v1 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[0]);
						currentThread->m_fTime = v1->Float;
						currentThread->m_sState = ScriptThread::TF_WTIME;
					}
					else
					{
						currentThread->m_sState = ScriptThread::TF_WCOND;
					}
				}
				//stop thread
				StopThread();
				stack->m_psStat = (Stat*)((char *)pstat + XCMD_SIZEOF(xcmd));
				currentThread->m_iBStackPos = bstack;
				return;

			case (OP_IMOVE):
			{
				IVariable* v2 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[1]);
				v2->Pointer = pstat->Op[0];

				if(DEBUG_STEP)
					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[1], true);
			}
			break;

			case (OP_INC):
			{
				IVariable* v1 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[0]);
				v1->Int++;

				if(DEBUG_STEP)
					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[0], true);
			}
			break;

			case (OP_DEC):
			{
				IVariable* v1 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[0]);
				v1->Int--;

				if(DEBUG_STEP)
					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[0], true);
			}
			break;

			case OP_POP:
			case OP_POPC:
			{
				int off = XCMD_OFFSET(xcmd);
				Variable* v1 = (Variable*)pstat->Op[0];
				enf_assert(v1->GetVType() == VARTYPE_INT || v1->GetVType() == VARTYPE_FLOAT || v1->GetVType() == VARTYPE_CLASS || v1->GetVType() == VARTYPE_CLASSREF);
				int* arr = v1->IsPtr() ? (int*)v1->Pointer : &v1->Int;
				for(int n = off - 1; n >= 0; n--)
					arr[n] = currentThread->PopData();
				currentThread->PopData();	//skip type (don't need to know during runtime)
			}
			break;

#define ENCODE_PUSHTYPE(t, s) ((t << 16) | s)

			case OP_PUSH:
			case OP_PUSHC:
			{
				int off = XCMD_OFFSET(xcmd);
				const Variable* v1 = (const Variable*)pstat->Op[0];
				VType vt = v1->GetVType();
				enf_assert(vt == VARTYPE_INT || vt == VARTYPE_FLOAT || vt == VARTYPE_CLASS || vt == VARTYPE_CLASSREF);
				currentThread->PushData(ENCODE_PUSHTYPE(vt, off));
				const int* arr = v1->IsPtr() ? (const int*)v1->Pointer : &v1->Int;
				for(int n = 0; n < off; n++)
					currentThread->PushData(arr[n]);
			}
			break;

			case OP_POPS:
			{
				int off = XCMD_OFFSET(xcmd);
				Variable* v1 = (Variable*)pstat->Op[0];
				char** arr = v1->IsPtr() ? (char**)v1->Pointer : &v1->String;
				for(int n = off - 1; n >= 0; n--)
				{				
					if(!IsStatic(arr[n]))
						FreeString(arr[n]);
					arr[n] = (char*)currentThread->PopData();
				}
				currentThread->PopData();	//skip type (don't need to know during runtime)
			}
			break;

			case OP_PUSHS:
			{
				int off = XCMD_OFFSET(xcmd);
				const Variable* v1 = (Variable*)pstat->Op[0];
				enf_assert(v1->GetVType() == VARTYPE_STRING);
				currentThread->PushData(ENCODE_PUSHTYPE(VARTYPE_STRING, off));
				const char*const* arr = v1->IsPtr() ? (const char*const*)v1->Pointer : &v1->String;
				for(int n = 0; n < off; n++)
				{				
					if(IsStatic(arr[n]))
						currentThread->PushData((int)arr[n]);
					else
						currentThread->PushData((int)AllocString(strlen(arr[n]) + 1, arr[n]));
				}
			}
			break;

			case OP_POPV:
			{
				int off = XCMD_OFFSET(xcmd);
				const Variable* v1 = (const Variable*)pstat->Op[0];
				{
					int* fp = (int *)v1->Pointer;
					for(int n = off - 1; n >= 0; n--)
					{
						fp[n * 3 + 2] = currentThread->PopData();
						fp[n * 3 + 1] = currentThread->PopData();
						fp[n * 3 + 0] = currentThread->PopData();
					}
				}
				currentThread->PopData();	//skip type (don't need to know during runtime)
			}
			break;

			case OP_PUSHV:
			{
				int off = XCMD_OFFSET(xcmd);
				const Variable* v1 = (Variable*)pstat->Op[0];
				enf_assert(v1->GetVType() == VARTYPE_VECTOR);
				currentThread->PushData(ENCODE_PUSHTYPE(VARTYPE_VECTOR, off));
				const int* fp = (const int *)v1->Pointer;
				for(int n = 0; n < off; n++)
				{
					currentThread->PushData(fp[n * 3 + 0]);
					currentThread->PushData(fp[n * 3 + 1]);
					currentThread->PushData(fp[n * 3 + 2]);
				}
			}
			break;

			case (OP_DEBUG):
#ifdef ENF_ENABLE_DEBUGGER
				{
				int offset = XCMD_OFFSET(xcmd);

				if(m_bEnableDebug && (offset & DBGF_BREAK || m_DebugMode != SDM_NONE))
				{
					if(m_DebugMode == SDM_STEP_TO)
					{
						if(offset & DBGF_TOCURSOR)
							const_cast<Stat*>(stack->m_psStat)->SetOffset(offset & ~DBGF_TOCURSOR);
						else if((offset & DBGF_BREAK) == 0)
							break;
					}

					StartDebugger(stack->m_pfFunc, m_pvThis, stack->GetDebugLine());
				}
				}
#endif
				break;

			case (OP_BEQ):
				if(sr == SF_ZERO)
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;


			case (OP_BNE):
				if(!(sr & SF_ZERO))
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;

			case (OP_BGE):
				if(!(sr & SF_NEG))
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;

			case (OP_BLE):
				if(sr & (SF_NEG | SF_ZERO))
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;

			case (OP_BLT):
				if(sr == SF_NEG)
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;

			case (OP_BGT):
				if(sr == 0)
				{
					stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
					continue;
				}
			break;

			case (OP_GOTO):
				stack->m_psStat = (Stat*)((int*)pstat + XCMD_OFFSET(xcmd));
				continue;

			case (OP_NEW):
				{
					ScriptModule* mod = (ScriptModule*)pstat->Op[0];
					stack->m_pfFunc->Module->_GetVariable(pstat->Op[1])->InstancePtr = mod->New((Class *)mod->GetVarType(XCMD_OFFSET(xcmd)), (Variable **)&pstat->Op[2]);
				}
			break;

			case (OP_DELETE):
				{
					IVariable* v1 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[0]);
					Instance* en = v1->InstancePtr;

					if(en == NULL)
						Error(ScriptContext::RTE_NullPointerError);

					if(en->GetClass()->ISizeof < sizeof(Instance) || en->GetClass()->ISizeof > 131072)
					{
						Error("Suspicious instance size upon delete.\nProbably not valid pointer!");
					}
#ifdef _DEBUG
					{
						CMutexLock lock(m_InstMutex);
						if(!m_Instances.Contains(en))
							Error(RTE_BadInstancePointer);
					}
#endif

					en->Delete();
					v1->InstancePtr = NULL;

					//get out of entity code:
					//there is not problem with local vars, as each thread has it's own stack, and
					//before sleep/wait has stored all of them
					while(en == stack->m_peInstance)
					{
						stack = PopCall(currentThread);
						if(currentThread->m_iStackPos == bstack)
							return;
					}
					pstat = stack->m_psStat;
				}
			break;

			case (OP_RTS):
				stack = PopCall(currentThread);

				if(currentThread->m_iStackPos == bstack)
					return;
				pstat = stack->m_psStat;
			}
//skip various number of parms. TODO: get it from xcmd, when possible!
			stack->m_psStat = (Stat*)((char *)pstat + pstat->GetSizeof());
		}
		else
		{
		//	if(cmd == OP_MOVEFR)
		//		cmd=cmd;

			Variable* ov1;
			Variable* ov2;
			IVariable* iv1 = stack->m_pfFunc->Module->_GetVariable2(pstat->Op[0], ov1);
			IVariable* iv2 = stack->m_pfFunc->Module->_GetVariable2(pstat->Op[1], ov2);
			if(cmd < OP_PARM2)
			{
				if(DEBUG_STEP)
				{
					bool cmpOnly = (cmd == OP_CMPV || cmd == OP_CMPI || cmd == OP_CMPF || cmd == OP_CMPS);

					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[0], false);
					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[1], !cmpOnly);
				}
				switch(cmd)
				{
				case (OP_CITF):
					iv2->Float = (float)iv1->Int;
					break;

				case (OP_CFTI):
					iv2->Int = (int)iv1->Float;
					break;

				case (OP_MOVE):
					iv2->Int = iv1->Int;
					break;

				case (OP_MOVEI):
					CheckTypeConversion(iv1, ov1, ov2);
					{
						Instance* i1 = iv1->InstancePtr;
						Instance* i2 = iv2->InstancePtr;

						if(i2 != i1)
						{
/*							if(i1)
							{
								i1->AddInstanceRef();
							}
							if(i2)
							{
								i2->ReleaseInstance();
							}*/
							iv2->InstancePtr = i1;
						}
					}
					break;

				case (OP_MOVEFR):
					if( !iv1->Pointer ) 
						iv2->InstancePtr = NULL;
					else
					{
						iv1 = ov1 = (Variable*)iv1->Pointer;
						CheckTypeConversion(iv1, ov1, ov2);
						iv2->InstancePtr = iv1->InstancePtr;
					}
					break;
				
				case (OP_MOVETR):
					iv2->Pointer = iv1;
					break;

				case (OP_MOVEA):
					if(iv2 != ov2)
					{
						if(ov1->Dim[0] != ov2->Dim[0] ||
							ov1->Dim[1] != ov2->Dim[1])
							Error("Attempt to resize instanced array");
						if(ov2->Spec & VSPEC_LOCAL)
							Error("Attempt to resize local array");
					}
					else
					{
						ov2->Dim[0] = ov1->Dim[0];
						ov2->Dim[1] = ov1->Dim[1];
					}

					enf_assert(iv1->Pointer);
					//v2->Pointer could be NULL, because of func parm arrays
					iv2->Pointer = iv1->Pointer;
					break;

				case (OP_MOVEV):
					{
					const float* pf1 = (const float*)iv1->Pointer;
					enf_assert(pf1);
					float* pf2 = (float*)iv2->Pointer;
					enf_assert(pf2);
					pf2[0] = pf1[0];
					pf2[1] = pf1[1];
					pf2[2] = pf1[2];
					}
					break;

				case (OP_MOVES):
					CopyString(iv1->String, ov2, &iv2->String, (ov1->Spec & VSPEC_OWNED) != 0);
					break;

				case (OP_CMPV):
					{
					const float* fv1 = (const float*)iv1->Pointer;
					enf_assert(fv1);
					const float* fv2 = (const float*)iv2->Pointer;
					enf_assert(fv2);

					if(fv1[0] == fv2[0] && fv1[1] == fv2[1] && fv1[2] == fv2[2])
						sr = SF_ZERO;
					else
						sr = 0;
					}
					break;

				case (OP_CMPI):
					{
					int l = iv2->Int - iv1->Int;
					if(l == 0)
						sr = SF_ZERO;
					else if(l < 0)
						sr = SF_NEG;
					else
						sr = 0;
					}
					break;

				case (OP_CMPF):
					{
					float f = iv2->Float - iv1->Float;
					if(f == 0)
						sr = SF_ZERO;
					else
					if(f < 0)
						sr = SF_NEG;
					else
						sr = 0;
					}
					break;

				case (OP_CMPS):
					//there shouldn't be possibility of two the same pointers to non-static string
					if(iv1->Pointer == iv2->Pointer)
					{
						sr = SF_ZERO;
					}
					else
					{
						enf_assert(iv1->String);
						enf_assert(iv2->String);

						int d = strcmp(iv2->String, iv1->String);
						if(d == 0)
							sr = SF_ZERO;
						else if(d < 0)
							sr = SF_NEG;
						else
							sr = 0;
					}
					break;

				case (OP_IFV):
					{
					int index = XCMD_OFFSET(xcmd);
#ifdef _DEBUG
					_CheckIndex(this, ov1, index);
#endif
					iv2->Int = ((int *)iv1->Pointer)[index];

					CheckTypeConversion(iv1, ov1, ov2);
					}
					break;

				case (OP_ITV):
					{
					int index = XCMD_OFFSET(xcmd);
#ifdef _DEBUG
					_CheckIndex(this, ov2, index);
#endif
					CheckTypeConversion(iv1, ov1, ov2);
					((int *)iv2->Pointer)[index] = iv1->Int;
					}
					break;

				case (OP_SIZEOF):
					iv2->Int = ov1->ArraySize();
					// achjo :-(
					if(ov1->GetVType() == VARTYPE_VECTOR)
						iv2->Int /= 3;
					break;
				}
//skip 2 parameters
				stack->m_psStat = (Stat*)((char *)pstat + 12);
			}
			else
			{
				IVariable* v3 = stack->m_pfFunc->Module->_GetVariable(pstat->Op[2]);

				if(DEBUG_STEP)
				{
					TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[0], false);
					if(cmd == OP_FFV || cmd == OP_FTV)
					{
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[1], true);
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[2], false);
					}
					else
					{
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[1], false);
						TouchVar(currentThread, stack->m_pfFunc->Module, (IVariable*)pstat->Op[2], true);
					}
				}

				switch(cmd)
				{
				case (OP_CLAMPI):
					v3->Int = enf_limit(v3->Int, iv1->Int, iv2->Int);
					break;

				case (OP_CLAMPF):
					v3->Float = enf_limit(v3->Float, iv1->Float, iv2->Float);
					break;

				case (OP_ANDI):
					{
					int l = iv1->Int & iv2->Int;
					v3->Int = l;
					if(l == 0)
						sr = SF_ZERO;
					else if(l < 0)
						sr = SF_NEG;
					else
						sr = 0;
					}
					break;

				case (OP_ORI):
					{
					int l = iv1->Int | iv2->Int;
					v3->Int = l;
					if(l == 0)
						sr = SF_ZERO;
					else if(l < 0)
						sr = SF_NEG;
					else
						sr = 0;
					}
					break;

				case (OP_ADDI):
					v3->Int = iv1->Int + iv2->Int;
					break;

				case (OP_LSLI):
					v3->Int = iv1->Int << iv2->Int;
					break;

				case (OP_ASRI):
					v3->Int = iv1->Int >> iv2->Int;
					break;

				case (OP_EORI):
					v3->Int = iv1->Int ^ iv2->Int;
					break;

				case (OP_ADDF):
					v3->Float = iv1->Float + iv2->Float;
					break;

				case (OP_ADDS):
					{
					char* str;
					const char* s1 = iv1->String;
					enf_assert(s1);
					const char* s2 = iv2->String;
					enf_assert(s2);

					uint l1 = strlen(s1);
					uint l2 = strlen(s2);
					uint l = l1 + l2 + 1;

					if(IsStatic(v3->Pointer))
					{
						str = (char *)AllocString(l, NULL);
						v3->Pointer = NULL;
					}
					else
					{
						if(v3 == iv1 || v3 == iv2 || l > MemSize(v3->Pointer))
						{
							str = (char *)AllocString(l, NULL);
						}
						else
						{
							str = v3->String;
							v3->Pointer = NULL;
						}
					}

					CopyMem(str, s1, l1);
					CopyMem(str + l1, s2, l2);
					str[l1 + l2] = 0;

					if(v3->Pointer != NULL)
						FreeString(v3->String);

					v3->Pointer = str;
					}
					break;

				case (OP_ADDV):
					{
					const float* pf1 = (const float*)iv1->Pointer;
					enf_assert(pf1);
					const float* pf2 = (const float*)iv2->Pointer;
					enf_assert(pf2);
					float* pf3 = (float*)v3->Pointer;
					enf_assert(pf3);
					pf3[0] = pf1[0] + pf2[0];
					pf3[1] = pf1[1] + pf2[1];
					pf3[2] = pf1[2] + pf2[2];
					}
					break;

				case (OP_SUBV):
					{
					const float* pf1 = (const float*)iv1->Pointer;
					enf_assert(pf1);
					const float* pf2 = (const float*)iv2->Pointer;
					enf_assert(pf2);
					float* pf3 = (float*)v3->Pointer;
					enf_assert(pf3);
					pf3[0] = pf1[0] - pf2[0];
					pf3[1] = pf1[1] - pf2[1];
					pf3[2] = pf1[2] - pf2[2];
					}
					break;

				case (OP_MODI):
					v3->Int = iv1->Int % iv2->Int;
					break;

				case (OP_SUBI):
					v3->Int = iv1->Int - iv2->Int;
					break;

				case (OP_SUBF):
					v3->Float = iv1->Float - iv2->Float;
					break;

				case (OP_MULI):
					v3->Int = iv1->Int * iv2->Int;
					break;

				case (OP_MULF):
					v3->Float = iv1->Float * iv2->Float;
					break;

				case (OP_SCALEV):
					{
					const float* pf1 = (const float*)iv1->Pointer;
					enf_assert(pf1);
					float* pf3 = (float*)v3->Pointer;
					enf_assert(pf3);
					float scale = iv2->Float;
					pf3[0] = pf1[0] * scale;
					pf3[1] = pf1[1] * scale;
					pf3[2] = pf1[2] * scale;
					}
					break;

				case (OP_CROSSV):
					{
					const float* pf1 = (const float*)iv1->Pointer;
					enf_assert(pf1);
					const float* pf2 = (const float*)iv2->Pointer;
					enf_assert(pf2);
					float x = pf1[1] * pf2[2] - pf1[2] * pf2[1];
					float y = pf1[2] * pf2[0] - pf1[0] * pf2[2];
					float z = pf1[0] * pf2[1] - pf1[1] * pf2[0];
					float* pf3 = (float*)v3->Pointer;
					enf_assert(pf3);
					pf3[0] = x;
					pf3[1] = y;
					pf3[2] = z;
					}
					break;

				case (OP_DOTV):
					{
					const float* pf1 = (const float *)(iv1->Pointer);
					enf_assert(pf1);
					const float* pf2 = (const float *)(iv2->Pointer);
					enf_assert(pf2);
					v3->Float = pf1[0] * pf2[0] + pf1[1] * pf2[1] + pf1[2] * pf2[2];
					}
					break;

				case (OP_DIVI):
					if(iv2->Int == 0)
						Error("Division by zero");
					v3->Int = iv1->Int / iv2->Int;
					break;

				case (OP_DIVF):
					if(iv2->Float == 0)
						Error("Division by zero");
					v3->Float = iv1->Float / iv2->Float;
					break;

				case (OP_CFV):
					{
					Variable* tmpv = ov2;

					_CheckIndex(this, ov1, (uint)v3->Int);

					if(tmpv->GetVType() == VARTYPE_STRING)
					{
						enf_assert(!tmpv->IsPtr());
						CopyString(((char **)iv1->Pointer)[v3->Int], ov2, &iv2->String, (ov1->Spec & VSPEC_OWNED) != 0);
					}
					else
					{
						int dim = tmpv->ArraySize();	
						if(dim <= 1)
							iv2->Int = ((int *)iv1->Pointer)[v3->Int];
						else
						{
							int size = m_VarTypes[tmpv->GetTypeIndex()]->Sizeof;
							CopyMem(iv2->Pointer,
									&((char *)iv1->Pointer)[v3->Int * dim * size],
									dim * size);
						}
						CheckTypeConversion(iv2, ov1, ov2); //check already transferred value, so don't need to handle array
					}
					}
					break;

				case (OP_CTV):
					{
					Variable* tmpv = ov1;

					_CheckIndex(this, ov2, (uint)v3->Int);

					if(tmpv->GetVType() == VARTYPE_STRING)
					{
						enf_assert(tmpv->ArraySize() <= 1);
						CopyString(iv1->String, ov2, &((char **)iv2->Pointer)[v3->Int], (ov1->Spec & VSPEC_OWNED) != 0);
					}
					else
					{
						CheckTypeConversion(iv1, ov1, ov2);
						int dim = tmpv->ArraySize();	
						if(dim <= 1)
							((int *)iv2->Pointer)[v3->Int] = iv1->Int;
						else
						{
							int size = m_VarTypes[tmpv->GetTypeIndex()]->Sizeof;
							CopyMem(&((char *)iv2->Pointer)[v3->Int * dim * size],
									iv1->Pointer,
									dim * size);
						}
					}
					}
					break;

				case (OP_FFV):
					_CheckIndex(this, ov1, (uint)v3->Int);
					iv2->Int = ((int *)iv1->Pointer)[v3->Int];
					CheckTypeConversion(iv2, ov1, ov2); //check already transferred value, so don't need to handle array
					break;

				case (OP_FTV):
					_CheckIndex(this, ov2, (uint)v3->Int);
					CheckTypeConversion(iv1, ov1, ov2);
					((int *)iv2->Pointer)[v3->Int] = iv1->Int;
					break;
				}
//skip 3 parameters
				stack->m_psStat++;
			}
		}
	}

#if defined(ENF_EXCEPTION) //&& !defined(_DEBUG)
	}

	catch(VMError)
	{
		stack = PopCall(currentThread);

		if(currentThread->m_iStackPos == bstack)
			return;
		
		//skip OP_CALL
		stack->m_psStat = (Stat*)((char *)stack->m_psStat + stack->m_psStat->GetSizeof());
	}

	catch(...)
	{
		Error("Unhandled exception");
	}
	}
#endif

}
//#pragma optimize("", on)

}
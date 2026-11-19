// IdFlags.h
//
// Author Roderick Kennedy @date 13/11/2005
#ifndef IdFlagsH
#define IdFlagsH
//---------------------------------------------------------------------------
#ifdef __GNUC__
	#define __int64 long
#endif
#include "Simul/Base/List.h"
#include "Simul/Base/Export.h"
#include <vector>

#define IDFLAGS_128_BIT

#ifdef IDFLAGS_128_BIT
/// \internal A bitfield.

/// \internal IdFlags is a bitfield used for boolean operations. It is 128 bits.
/// As IdFlags has 128 bits, it allows for (for example) 128 surfaces in a SimulVolume, or 128
/// collideables in a SweepAndPruneBroadphase.
SIMUL_BASE_EXPORT_CLASS IdFlags
{
public:
	unsigned __int64 First64Bits;
	unsigned __int64 Second64Bits;
	class FlagOverflow{};
	IdFlags();
	IdFlags(unsigned __int64 num);
	operator size_t() const
	{
		return (size_t(First64Bits));
	}
	operator int() const
	{
		return (int(First64Bits));
	}
	operator bool() const
	{
		return (First64Bits!=0||Second64Bits!=0);
	}
	bool operator==(const IdFlags &num);   
	bool operator!=(const IdFlags &num);
	bool operator==(bool flag);
	bool operator<(const IdFlags &num) const;
	bool operator!=(bool flag);
	IdFlags& operator=(unsigned __int64 num);
	IdFlags operator&(const IdFlags &Other) const;
	IdFlags operator|(const IdFlags &Other) const;
	IdFlags operator~();
	void operator^=(const IdFlags &Other);
	void operator&=(const IdFlags &Other);
	void operator-=(const IdFlags &Other);
	void operator|=(const IdFlags &Other);
	IdFlags operator>>(int i);
	void operator<<=(int i);
	bool operator!() const;
	static int BitSize(){return 128;}
	friend SIMUL_BASE_EXPORT IdFlags IntToFlag(int Value);
};
#else
typedef unsigned __int64 IdFlags;
#endif
///  \brief Shift the bits left by one bit.
inline void ShiftLeftBy1(IdFlags &F)
{
	F<<=1;
}
///  \brief Make F equal to 1<<j.
inline void LeftShift1(IdFlags &F,int j)
{
	char i=(char)j;
#if defined(WIN32) && !defined(__GNUC__)
	_asm
	{
		xorps xmm1,xmm1
		mov ebx,F
		movups [ebx],xmm1	// zero out the flags
		mov		eax,1
// Handle shifts of between 0 and 31 bits
		mov		cl,i
        cmp     cl,32
        jae     more_than_32
        shl     eax,cl
		mov		[ebx],eax
		jmp end_of_fn
// Handle shifts of between 32 and 63 bits
more_than_32:
        cmp     cl,64
        jae     more_than_64
		and		cl,31
        shl     eax,cl
		mov		[ebx+4],eax
		jmp end_of_fn
// Handle shifts of between 32 and 63 bits
more_than_64:
        cmp     cl,96
        jae     more_than_96
		and		cl,31
        shl     eax,cl
		mov		[ebx+8],eax
		jmp end_of_fn
// Handle shifts of between 32 and 63 bits
more_than_96:
		and		cl,31
        shl     eax,cl
		mov		[ebx+12],eax
end_of_fn:
	}
#else
	if(i<64)
		F=((unsigned __int64)1)<<i;
	else
	{
		F.First64Bits=0;
		F.Second64Bits=((unsigned __int64)1)<<(i-64);
	}
#endif
}
extern SIMUL_BASE_EXPORT IdFlags OneBitShifted[];
///  \brief Set \em bit of F.
inline void SetBit(IdFlags &F,int bit)
{
#ifdef _DEBUG
	if(bit>=IdFlags::BitSize())
		throw IdFlags::FlagOverflow();
#endif
#if defined(WIN32) && !defined(__GNUC__)
	_asm
	{
		mov ebx,bit
		mov eax,F
		bts [eax],ebx
	}
#else
	F|=OneBitShifted[bit];
#endif
}
///  \brief Clear \em bit of Flags.
inline void ClearBit(IdFlags &Flags,int bit)
{
#ifdef _DEBUG
	if(bit>=IdFlags::BitSize())
		throw IdFlags::FlagOverflow();
#endif
#if defined(WIN32) && !defined(__GNUC__)
	_asm
	{
		mov ebx,bit
		mov eax,Flags
		btr [eax],ebx
	}
#else
	Flags&=~(OneBitShifted[bit]);
#endif
}
/// \brief return the value of \em bit.
inline bool IsBitSet(const IdFlags &Flags,int bit)
{
#if !defined(WIN32) || defined(__GNUC__)
	return((Flags&(OneBitShifted[bit])));
#else
	_asm
	{
		push ebx
		mov eax,0
		mov ebx,bit
		mov edx,Flags
		cmp ebx,32
		jnb test_32
		mov ecx,[edx]
		and ebx,31
		bt ecx,ebx
		jc ret_true
		jmp ret_false
test_32:
		cmp ebx,64
		jnb test_64
		mov ecx,[edx+4]
		and ebx,31
		bt ecx,ebx
		jc ret_true
		jmp ret_false
test_64:
		cmp ebx,96
		jnb test_96
		mov ecx,[edx+8]
		and ebx,31
		bt ecx,ebx
		jc ret_true
		jmp ret_false
test_96:
		mov ecx,[edx+12]
		and ebx,31
		bt ecx,ebx
		jc ret_true
		jmp ret_false
ret_true:
		mov eax,1
ret_false:
		pop ebx
	}
#endif
}
///  \brief Swap the bits at \em i and \em j.
inline void SwapFlags(IdFlags &Flags,int i,int j)
{
	bool i1=IsBitSet(Flags,i);
	if(IsBitSet(Flags,j))
		SetBit(Flags,i);
	else
		ClearBit(Flags,i);
	if(i1)
		SetBit(Flags,j);
	else
		ClearBit(Flags,j);
}
///  \brief Move bit \em i to \em j, and clear bit \em i.
inline void MoveBit(IdFlags &Flags,int i,int j)
{
	if(IsBitSet(Flags,i))
		SetBit(Flags,j);
	else
		ClearBit(Flags,j);
	ClearBit(Flags,i);
}
extern int SIMUL_BASE_EXPORT_FN FlagsToSingleId(const IdFlags &Flags);
///  \brief Return 1 << \em i.
inline IdFlags LeftShift1(int i)
{
	IdFlags f;
#ifdef IDFLAGS_128_BIT
	if(i<64)
		f=((unsigned __int64)1)<<i;
	else
	{
		f.First64Bits=0;
		f.Second64Bits=((unsigned __int64)1)<<(i-64);
	}
	return f;
#else
	f=((IdFlags)1)<<i;
	return f;
#endif
}
extern SIMUL_BASE_EXPORT IdFlags OneBitShifted[128];
///  \brief Find the first set bit from \em i, return its index and clear the bit.
inline char ExtractFirstId(IdFlags &Flags,size_t i)
{
	if(!Flags)
		return -2;
	for(;i<128;i++)
	{
		if(!(!(Flags&OneBitShifted[(size_t)i])))
		{
			Flags-=OneBitShifted[(size_t)i];
			return (char)i;
		}
	}
	return -2;
}
extern int		SIMUL_BASE_EXPORT_FN GetFirstId(const IdFlags &Flags);			///< Return the position of the first non-zero bit.
extern int		SIMUL_BASE_EXPORT_FN GetNextId(const IdFlags &Flags,int i);		///< Return the position of the next non-zero bit after bit i.
extern IdFlags	SIMUL_BASE_EXPORT_FN IntListToFlags(std::vector<int>&IntList);
extern IdFlags	SIMUL_BASE_EXPORT_FN IntToFlag(int Value);
extern int		SIMUL_BASE_EXPORT_FN GetInt(const IdFlags &f);
extern IdFlags	SIMUL_BASE_EXPORT_FN MaskBitsTo(int i);
extern int		SIMUL_BASE_EXPORT_FN NumFlags(const IdFlags &f);
extern void		SIMUL_BASE_EXPORT_FN CutFlag(IdFlags &f,int i);					///< Remove the bit at i, shift all bits above it down 1.

#endif

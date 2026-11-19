//-----------------------------------------------------------------------------
// File: enf_memoryallocator.cpp
//
// Desc: Memory allocator for small blocks
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "system/enf_memory.h"
#include "common/enf_types.h"
#include "common/enf_set.h"
#include "system/enf_mutex.h"
#include "enf_main.h"
#include <stdio.h>

#ifdef ENF_LEAK_DETECTOR
	#include "common/enf_memoryreport.h"
	#ifdef ENF_ALLOC_STACKTRACE
		#include "Dbghelp.h"
	#endif
#endif

namespace enf
{

const uint ENF_DEBUG_FULL = 0xdebaf001;
const uint ENF_DEBUG_FREE = 0xdebafeee;

const uint MEMALLOC_BLOCK		= 4000;		//minimum size of block
const uint MEMALLOC_MIN_ITEMS	= 256;		//minimum items in block

class PUBLIC_API ISmallBlockAllocator: public SmallBlockAllocator
{
private:
	class MemoryBlock;

	CMutex	m_Mutex;

	//!granularity of allocator
	static const unsigned int QUANTIZE	= 8;
	//!Max size of item
	static const unsigned int MAX_SIZE	= 512;

	//!Max blocks needed for that
	static const int MAX_BLOCKS = (MAX_SIZE / QUANTIZE);

	MemoryBlock*		m_pBlocks[MAX_BLOCKS];
	Set<const void*>	m_pSortedBlocks;

	size_t	m_iAllocated;
	size_t	m_iNumAllocations;
	size_t	m_iTotalAllocated;

	//-----------------------------------------------------------------------------------
	// quantize size
	//-----------------------------------------------------------------------------------
	static ENF_INLINEFUNC size_t FixSize(size_t size)
	{
		return ((size + (QUANTIZE - 1)) & (~(QUANTIZE - 1)));
	}

public:
	//memory item 
	struct MemItem
	{
		MemItem* m_pNext;
	};

	//struct for one memory block containing items, struct is allocated 
	//m_iHeadSize memory = sizeof(BlockHead) + m_iBlockSize -> the pointer 
	//to real data is &BlockHead + sizeof(BlockHead)
	//Padded to 32byte, to keep memory block 16-byte aligned
	struct BlockHead
	{
		MemoryBlock*	m_pMemoryBlock;		//our owner
		MemItem*			m_pFirstFree;			//pointer to first free item in block
		BlockHead*		m_pNextBlock;			//pointer to next memory block
		BlockHead**		m_pPreviousBlock;		//pointer to prev memory block
		int				m_iFullItems;			//number of full items, max can be m_iBlockItems, then it is necessary to create new block
		int				m_iPad0;
		int				m_iPad1;
		int				m_iPad2;
	};

#ifdef ENF_LEAK_DETECTOR
	//struct for debug
	struct DebugMemory
	{
		uint			m_iDummy;		//testing int
		const char* m_pFileName;	//name of file
		uint			m_iLine;			//line in source file
		uint			m_iID;
#ifdef ENF_ALLOC_STACKTRACE
		intptr_t		m_Stack[MAX_STACK_TRACE];
#endif
	};

	uint CreateSnapshot(AllocationSnapshot& snap) const;
#endif

	ISmallBlockAllocator();
	~ISmallBlockAllocator();

	ENF_INLINE bool Register(size_t size);
	ENF_INLINE bool UnRegister(size_t size);

	void GetStats(MemoryStats& stats) const;

	void Dump(const char* str, unsigned int flags) const;

	void Flush();

	bool CheckPointer(void *pointer);

	void* Alloc(size_t size, const char* file, int line);
	bool Free(void *pointer);
	void FreeAll();
};

ISmallBlockAllocator g_SmallBlockAllocator;
SmallBlockAllocator* g_pSmallBlockAllocator = &g_SmallBlockAllocator;

//-----------------------------------------------------------------------------------
// SmallBlockAllocator for managing small memory allocs
//-----------------------------------------------------------------------------------
class ISmallBlockAllocator::MemoryBlock
{
private:
	friend class ISmallBlockAllocator;

	ISmallBlockAllocator*	m_pAllocator;	//our owner
	size_t		m_iItemSize;				//registered size of item
	size_t		m_iBlockSize;				//size of whole block
	size_t		m_iHeadSize;				//size of data and block together (is in one memory area)
	size_t		m_iBlockItems;				//size of one item in block, must be bigger than 3 and m_iBlockSize is divisible by 

	size_t		m_iRegSize;					//registered size

	BlockHead*	m_pFirstBlock;				//first block with items
	BlockHead*	m_pFreeBlock;				//block with free items

protected:
	ENF_INLINE MemoryBlock(ISmallBlockAllocator* allocator, size_t size); 
	~MemoryBlock();
	
	//-----------------------------------------------------------------------------------
	// Name : Release
	// Desc : release all memory allocated by manager
	//-----------------------------------------------------------------------------------
	ENF_INLINE void Release();

	//-----------------------------------------------------------------------------------
	// Name : Create
	// Desc : create new
	//-----------------------------------------------------------------------------------
	ENF_INLINE void Create(size_t size);

	//-----------------------------------------------------------------------------------
	// Name : AllocNewBlock
	// Desc : create new block
	//-----------------------------------------------------------------------------------
	ENF_INLINE BlockHead* AllocNewBlock();

	//-----------------------------------------------------------------------------------
	// Name : AllocNewBlock
	// Desc : create new block
	//-----------------------------------------------------------------------------------
	ENF_INLINE void ReleaseBlock(BlockHead* block);

	//-----------------------------------------------------------------------------------
	// Name : GetBestFreeBlock
	// Desc : returns the block with the largest number of unused items
	//-----------------------------------------------------------------------------------
	ENF_INLINE void FindBestFreeBlock();


	#ifdef ENF_LEAK_DETECTOR
		void DebugInfo (DebugMemory* p, int info, const char *file = NULL, int num = -1);
		void TestDebugInfo(DebugMemory *p, int info);
		void DumpMemory(BlockHead *pblock, unsigned int flags);
		uint CreateSnapshot(BlockHead *pblock, AllocationSnapshot& snap);
	#endif

public:
	ENF_DECLARE_MMOBJECT(MemoryBlock);

	//-----------------------------------------------------------------------------------
	// Name : Alloc
	// Desc : return pointer to new allocated memory 
	//-----------------------------------------------------------------------------------
		ENF_INLINE void *Alloc(const char *file, int line);

	//-----------------------------------------------------------------------------------
	// Name : FreeAll
	// Desc : deallocate all the allocated the memory 
	//-----------------------------------------------------------------------------------
	ENF_INLINE void FreeAll(void *pointer);


	//-----------------------------------------------------------------------------------
	// Name : GetInstanceSize
	// Desc : returns instance size in memory (with all allocated items)
	//-----------------------------------------------------------------------------------
	ENF_INLINE size_t GetInstanceSize() const;
};

//-----------------------------------------------------------------------------------
// constructor
//-----------------------------------------------------------------------------------
ISmallBlockAllocator::MemoryBlock::MemoryBlock(ISmallBlockAllocator* allocator, size_t size):
	m_pAllocator(allocator),
	m_pFirstBlock(NULL)
{
	Create (size);
}

//-----------------------------------------------------------------------------------
// destructor
//-----------------------------------------------------------------------------------
ISmallBlockAllocator::MemoryBlock::~MemoryBlock()
{
	Release();
}

//-----------------------------------------------------------------------------------
// Name : Release
// Desc : release all memory allocated by manager
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::Release()
{
	BlockHead* next = m_pFirstBlock, *tmp;

	//loop through all blocks
	while (next)
	{
		tmp  = next;
		next = next->m_pNextBlock;
		m_pAllocator->m_pSortedBlocks.Remove(tmp);
		MemoryManager::Free16(tmp);
		m_pAllocator->m_iTotalAllocated -= m_iHeadSize;
	}

	m_pFirstBlock = NULL;
}

//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::Flush()
{
	if(!m_Mutex.TryLock())
		return;

	for(uint n = 0; n < MAX_BLOCKS; n++)
	{
		MemoryBlock* block = m_pBlocks[n];
		if(block)
		{
			BlockHead* next;
			for(BlockHead* head = block->m_pFirstBlock; head; head = next)
			{
				next = head->m_pNextBlock;

				if (head->m_iFullItems == 0)
				{
					block->ReleaseBlock(head);
				}
			}
		}
	}
	m_Mutex.Unlock();
}

//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::GetStats(MemoryStats& stats) const
{
	stats.Allocated = m_iAllocated;
	stats.NumAllocations = m_iNumAllocations;
	stats.NumBlocks = m_pSortedBlocks.GetCardinality();
	stats.Overhead = (m_iTotalAllocated + m_pSortedBlocks.GetCardinality() * sizeof(void*)) - m_iAllocated;
}

//-----------------------------------------------------------------------------------
// Name : Create
// Desc : create new
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::Create(size_t size)
{
	Release();

	m_iRegSize =  size;

	//set size
	size = enf_max((unsigned int)size, 4U);

	#ifdef ENF_LEAK_DETECTOR
		//4 bytes dummy object == ENF_DEBUG_NUM
		//4 bytes pointer to filename __FILE__
		//4 bytes pointer to line in filename__NUM__
		size += sizeof(DebugMemory);
	#endif

	m_iItemSize		= size;

	//compute size of block
	if (m_iItemSize * MEMALLOC_MIN_ITEMS < MEMALLOC_BLOCK)
		m_iBlockSize = MEMALLOC_BLOCK;
	else
		m_iBlockSize = ((m_iItemSize * MEMALLOC_MIN_ITEMS) / MEMALLOC_BLOCK + 1) * MEMALLOC_BLOCK;

	m_iBlockItems = m_iBlockSize/size;
	m_iHeadSize = sizeof(BlockHead) + m_iBlockSize;
	m_pFreeBlock = m_pFirstBlock = AllocNewBlock();
	m_pFirstBlock->m_pPreviousBlock = &m_pFirstBlock;
}


//-----------------------------------------------------------------------------------
// Name : AllocNewBlock
// Desc : create new block
//-----------------------------------------------------------------------------------
ISmallBlockAllocator::BlockHead* ISmallBlockAllocator::MemoryBlock::AllocNewBlock()
{
	//create new block head
	BlockHead *newone = (BlockHead*)MemoryManager::Alloc16(m_iHeadSize, __FILE__, __LINE__);
	m_pAllocator->m_iTotalAllocated += m_iHeadSize;

	m_pAllocator->m_pSortedBlocks.Insert(newone);
	newone->m_pMemoryBlock = this;
	newone->m_pPreviousBlock = NULL;
	newone->m_pNextBlock = NULL;
	newone->m_iFullItems = 0;

	//link items inside block
	ubyte *pstart = (ubyte*) newone + sizeof(BlockHead);

	newone->m_pFirstFree = (MemItem*) pstart;

	for (uint i = 0; i < m_iBlockItems - 1; i++)
	{
		//pointer to next free block
		*((void**) pstart) = (void*) (pstart + m_iItemSize);

		#ifdef ENF_LEAK_DETECTOR
			DebugInfo((DebugMemory*) (pstart + m_iRegSize), ENF_DEBUG_FREE);
		#endif

		pstart += m_iItemSize;
	}


	//pointer to next free block
	*((uint*) pstart) = NULL;

	#ifdef ENF_LEAK_DETECTOR
	DebugInfo((DebugMemory*) (pstart + m_iRegSize), ENF_DEBUG_FREE);
	#endif

	return newone;
}


//-----------------------------------------------------------------------------------
// Name : ReleaseBlock
// Desc : releases block
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::ReleaseBlock(BlockHead* block)
{
	//unlink with previous
	*block->m_pPreviousBlock = block->m_pNextBlock;

	//unlink with next
	if (block->m_pNextBlock)
		block->m_pNextBlock->m_pPreviousBlock = block->m_pPreviousBlock;

	//find best free block
	if (m_pFreeBlock == block)
		FindBestFreeBlock();

	m_pAllocator->m_pSortedBlocks.Remove(block);
	//delete
	MemoryManager::Free16(block);
	m_pAllocator->m_iTotalAllocated -= m_iHeadSize;
}

//-----------------------------------------------------------------------------------
// Name : GetBestFreeBlock
// Desc : returns the block with the largest number of unused items
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::FindBestFreeBlock() 
{ 
	BlockHead* pstart = m_pFirstBlock;

	//loop through all items
	while (pstart)
	{
		if (pstart->m_pFirstFree)
		{
			m_pFreeBlock = pstart;
			return;
		}

		pstart = pstart->m_pNextBlock;
	}

	//create new
	pstart = AllocNewBlock();

	//and link it at the begin
	pstart->m_pPreviousBlock = &m_pFirstBlock;
	pstart->m_pNextBlock = m_pFirstBlock;
	if(m_pFirstBlock)
		m_pFirstBlock->m_pPreviousBlock = &pstart->m_pNextBlock;
	m_pFreeBlock = m_pFirstBlock = pstart;
}


#ifdef ENF_ALLOC_STACKTRACE
	void FillStackTrace(intptr_t* ptr, int maxSize, int skip);
#endif

//-----------------------------------------------------------------------------------
// Name : Alloc
// Desc : alloc new
//-----------------------------------------------------------------------------------
void *ISmallBlockAllocator::MemoryBlock::Alloc(const char *file, int line)
{
	MemItem* pfree = m_pFreeBlock->m_pFirstFree;

	#ifdef ENF_LEAK_DETECTOR
		DebugMemory* deb = (DebugMemory*) ((ubyte*) pfree + m_iRegSize);

		TestDebugInfo(deb, ENF_DEBUG_FREE);
		DebugInfo(deb, ENF_DEBUG_FULL, file, line);
	#endif

	m_pFreeBlock->m_pFirstFree = pfree->m_pNext;
	m_pFreeBlock->m_iFullItems++;

	#ifdef _DEBUG
		//fill allocated memory by 0xbaadf00d as MSVC does
		FillMemory32(pfree, (m_iRegSize / 4), 0x7fcdf00d);
	#endif

		//if we are on the end of block find another block with the most free items for another allocation
	if (!m_pFreeBlock->m_pFirstFree)
		FindBestFreeBlock();	

	return pfree;
}

//-----------------------------------------------------------------------------------
// Name : FreeAll
// Desc : deallocate all the allocated the memory 
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::FreeAll(void *pointer)
{
	Release();
	
	m_pFreeBlock = m_pFirstBlock = AllocNewBlock();
	m_pFirstBlock->m_pPreviousBlock = &m_pFirstBlock;
}

//-----------------------------------------------------------------------------------
// Name : GetInstanceSize
// Desc : returns instance size in memory
//-----------------------------------------------------------------------------------
size_t ISmallBlockAllocator::MemoryBlock::GetInstanceSize() const
{
	int num = 0;

	BlockHead* pblock = m_pFirstBlock;

	while (pblock)
	{
		pblock = pblock->m_pNextBlock;
		num++;
	}

	return sizeof(MemoryBlock) + num * m_iHeadSize;
}


#ifdef ENF_LEAK_DETECTOR

//-----------------------------------------------------------------------------------
// Name : DebugInfo
// Desc : fill to p the debug info
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::DebugInfo (DebugMemory* p, int info, const char *file, int num)
{
	p->m_iDummy		= info;
	p->m_pFileName	= file;
	p->m_iLine		= num;
	p->m_iID			= MemoryManager::GetAllocationID();
#ifdef ENF_ALLOC_STACKTRACE
	FillStackTrace(p->m_Stack, MAX_STACK_TRACE, 2);
#endif
}

//-----------------------------------------------------------------------------------
// Name : DebugInfo
// Desc : fill to p the debug info
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::TestDebugInfo(DebugMemory *p, int info)
{
	if (p->m_iDummy == info)
		return;

	char str[512];

	if (p->m_pFileName)
		sprintf(str, "SmallBlockAllocator::MemoryBlock(%d) memory overwritten in file %s on line %d", (int)m_iRegSize, p->m_pFileName, p->m_iLine);
	else
		sprintf(str, "SmallBlockAllocator::MemoryBlock(%d) memory overwritten, no file info available", (int)m_iRegSize);

	LogF(LL_DEBUG, str);

	enf_assert (p->m_iDummy == info);
}

//-----------------------------------------------------------------------------------
// Name : DebugInfo
// Desc : fill to p the debug info
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::MemoryBlock::DumpMemory(BlockHead *pblock, unsigned int flags)
{
	if (!pblock)
		return;

	size_t cnt = 0, unalloc = 0;
	size_t unknown = 0;
	char str[512];
	
	AllocationSet chunks;

	//test all available blocks, should be just one in correct case
	while (pblock)
	{
		if (pblock->m_iFullItems)
		{
 			unalloc += pblock->m_iFullItems;

			ubyte *pstart = (ubyte*) pblock + sizeof(BlockHead);

			//test all inside, debug flag must be ENF_DEBUG_FREE
			for (uint i = 0; i < m_iBlockItems; i++)
			{
				//test if the memory was correctly released
				DebugMemory* deb = (DebugMemory*) (pstart + m_iRegSize);
				if (deb->m_iDummy != ENF_DEBUG_FREE)
				{
#ifdef ENF_ALLOC_STACKTRACE
					chunks.Insert(deb->m_iID, deb->m_pFileName, deb->m_iLine, m_iRegSize, pstart, deb->m_Stack);
#else
					chunks.Insert(deb->m_iID, deb->m_pFileName, deb->m_iLine, m_iRegSize, pstart);
#endif

					if(deb->m_pFileName == NULL)
					{
						unknown += m_iRegSize;
					}
				}
				pstart += m_iItemSize;
			}
		}

		pblock = pblock->m_pNextBlock;
	}

	if (unalloc)
	{
		if((flags & MD_NODATA) == 0)
		{
				LogF(LL_DEBUG, "Block size %d, %d item/s were not deallocated : \n", (int)m_iRegSize, (int)unalloc);
		}
		chunks.Dump(flags);
/*
		//write all
		for (uint i = 0; i < chunks.GetCardinality(); i++)
		{
			sprintf (str, "  %s(%d) %8d %8d\n", chunks[i].File, chunks[i].Line, chunks[i].Count, chunks[i].Size);
			LogF(LL_DEBUG, str);
		}
*/
		if(unknown)
		{
			LogF(LL_DEBUG,"  unknown source, %d KB \n", (int)unknown);
		}
	}
}


//-----------------------------------------------------------------------------
/*!
snapshot
*/
//-----------------------------------------------------------------------------
uint ISmallBlockAllocator::MemoryBlock::CreateSnapshot(BlockHead *pblock, AllocationSnapshot &snap)
{
	if (!pblock)
		return 0;

	size_t unalloc = 0;
	
	while (pblock)
	{
		if (pblock->m_iFullItems)
		{
 			unalloc += pblock->m_iFullItems;

			ubyte *pstart = (ubyte*) pblock + sizeof(BlockHead);

			//test all inside, debug flag must be ENF_DEBUG_FREE
			for (uint i = 0; i < m_iBlockItems; i++)
			{
				DebugMemory* deb = (DebugMemory*) (pstart + m_iRegSize);
				if (deb->m_iDummy != ENF_DEBUG_FREE)
				{
					snap.Insert(deb->m_pFileName, deb->m_iLine, m_iRegSize);
				}
				pstart += m_iItemSize;
			}
		}

		pblock = pblock->m_pNextBlock;
	}

	return unalloc*m_iRegSize;
}



//-----------------------------------------------------------------------------
/*!
snapshot
*/
//-----------------------------------------------------------------------------
uint ISmallBlockAllocator::CreateSnapshot(AllocationSnapshot &snap) const
{
	CMutexLock lock(m_Mutex);

	uint size = 0;

	snap.Clear();
	for (int i = 1; i < MAX_BLOCKS; i++)
	{
		if (m_pBlocks[i])
		{
			size += m_pBlocks[i]->CreateSnapshot(m_pBlocks[i]->m_pFirstBlock, snap);
		}
	}

	return size;
}

#endif

//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::Dump(const char* title, unsigned int flags) const
{
#ifdef ENF_LEAK_DETECTOR
	CMutexLock lock(m_Mutex);

	for (int i = 1; i < MAX_BLOCKS; i++)
	{
		if (m_pBlocks[i])
		{
			if(title)
			{
				LogF(LL_DEBUG, title);
				title = NULL;
			}
			m_pBlocks[i]->DumpMemory(m_pBlocks[i]->m_pFirstBlock, flags);
		}
	}
#endif
}

//-----------------------------------------------------------------------------------
// constructor, just clears the memory
//-----------------------------------------------------------------------------------
ISmallBlockAllocator::ISmallBlockAllocator():
	m_iAllocated(NULL),
	m_iTotalAllocated(NULL),
	m_iNumAllocations(NULL)
{ 
	memset(&m_pBlocks, 0, sizeof(MemoryBlock*) * MAX_BLOCKS); 
}


//-----------------------------------------------------------------------------------
// destructor, free
//-----------------------------------------------------------------------------------
ISmallBlockAllocator::~ISmallBlockAllocator() 
{ 
	FreeAll();
}

//-----------------------------------------------------------------------------------
// Name : Register
// Desc : register the size for memory allocator, returns true if successfully 
// registered, false if the size has been already registered
//-----------------------------------------------------------------------------------
bool ISmallBlockAllocator::Register(size_t size)
{
	CMutexLock lock(m_Mutex);

	size = FixSize(size);
	uint block = size / QUANTIZE;

	enf_assert (size > 0 && (size / QUANTIZE) < MAX_BLOCKS);

	if (size < 1 || block >= MAX_BLOCKS || m_pBlocks[block])
		return false;

	m_pBlocks[block] = ENF_NEW MemoryBlock(this, size);

	return true;
}


//-----------------------------------------------------------------------------------
// Name : UnRegister
// Desc : unregister the size for memory allocator, returns true if successfully 
// unregistered, false if the size has not been registered
//-----------------------------------------------------------------------------------
bool ISmallBlockAllocator::UnRegister(size_t size)
{
	CMutexLock lock(m_Mutex);

	size = FixSize(size);
	uint block = size / QUANTIZE;

	enf_assert (size > 0 && block < MAX_BLOCKS);

	if (size < 1 || block >= MAX_BLOCKS || !m_pBlocks[block])
		return false;

	SAFE_DELETE(m_pBlocks[block]);
	return true;
}

//-----------------------------------------------------------------------------------
// Name : Alloc(size)
// Desc : alloc memory of requested size
//-----------------------------------------------------------------------------------
void *ISmallBlockAllocator::Alloc(size_t size, const char* file, int line)
{
	CMutexLock lock(m_Mutex);
	
	size = FixSize(size);
	uint block = size / QUANTIZE;

	enf_assert2(size > 0 && block < MAX_BLOCKS, "SmallBlockAllocator::Alloc size must be 1-512 bytes");

	m_iAllocated += size;
	m_iNumAllocations++;

	if (!m_pBlocks[block])
		Register(size);

	return m_pBlocks[block]->Alloc(file, line);
}

//-----------------------------------------------------------------------------------
bool ISmallBlockAllocator::CheckPointer(void *pointer)
{
	CMutexLock lock(m_Mutex);
	int index = m_pSortedBlocks.FindGE((const MemoryBlock*)pointer);
	if(index == -1)
		return false;

	BlockHead* head = (BlockHead*)m_pSortedBlocks[index];

	//just to be sure, that we are from this block
	if((ubyte*)pointer >= (ubyte*)(head + 1) &&  (ubyte*)pointer < ((ubyte*)(head + 1) + head->m_pMemoryBlock->m_iBlockSize))
	{
#ifdef ENF_LEAK_DETECTOR
		DebugMemory* deb = (DebugMemory*) ((ubyte*) pointer + head->m_pMemoryBlock->m_iRegSize);
		head->m_pMemoryBlock->TestDebugInfo(deb, ENF_DEBUG_FULL);
		if(deb->m_iDummy != ENF_DEBUG_FREE)
			return true;
#endif
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------------
// Name : Release(pointer, size)
// Desc : release memory, return true if successfully
//-----------------------------------------------------------------------------------
bool ISmallBlockAllocator::Free(void *pointer)
{
	CMutexLock lock(m_Mutex);
	int index = m_pSortedBlocks.FindGE((const MemoryBlock*)pointer);
	if(index == -1)
		return false;

	BlockHead* head = (BlockHead*)m_pSortedBlocks[index];

	//just to be sure, that we are from this block
	enf_assert2((ubyte*)pointer >= (ubyte*)(head + 1) &&  (ubyte*)pointer < ((ubyte*)(head + 1) + head->m_pMemoryBlock->m_iBlockSize),
		"Trying to unallocate memory not from this heap");

	m_iAllocated -= head->m_pMemoryBlock->m_iRegSize;
	m_iNumAllocations--;

#ifdef ENF_LEAK_DETECTOR
	DebugMemory* deb = (DebugMemory*) ((ubyte*) pointer + head->m_pMemoryBlock->m_iRegSize);
	head->m_pMemoryBlock->TestDebugInfo(deb, ENF_DEBUG_FULL);
	deb->m_iDummy = ENF_DEBUG_FREE;
#endif

#ifdef _DEBUG
	//fill freed memory by 0xfeeefeee as MSVC does
	FillMemory32(pointer, (head->m_pMemoryBlock->m_iRegSize / 4), 0xfeeefeee);
#endif

	//retype and link with free items
	((MemItem*) pointer)->m_pNext	= head->m_pFirstFree;
	head->m_pFirstFree = (MemItem*) pointer;

	head->m_iFullItems--;

	//the pointer was removed more then once
	enf_assert(head->m_iFullItems >= 0);

	//if it's empty, but it's not the last one, release it
	if (head->m_iFullItems == 0 && (head != head->m_pMemoryBlock->m_pFirstBlock || head->m_pNextBlock != NULL))
		head->m_pMemoryBlock->ReleaseBlock(head);

	return true;
}

//-----------------------------------------------------------------------------------
// Name : FreeAll()
// Desc : release whole memory allocated by alocator, return true if successfully
//-----------------------------------------------------------------------------------
void ISmallBlockAllocator::FreeAll()
{
	CMutexLock lock(m_Mutex);

	for (int i = 1; i < MAX_BLOCKS; i++)
	{
		if (m_pBlocks[i])
		{
			SAFE_DELETE(m_pBlocks[i]);
		}
	}
	m_pSortedBlocks.Clear();
}

}
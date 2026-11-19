//-----------------------------------------------------------------------------
// File: enf_hashmap.h
//
// Desc: Hash map
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------
#pragma once
#ifndef ENF_HASHMAP_H
#define ENF_HASHMAP_H

#include "enf_config.h"
#include "common/enf_inttypes.h"
#include "system/enf_memory.h"

namespace enf
{
/** @addtogroup Containers
@{*/

//piss off with truncation warning :-P
#pragma warning( disable : 4311 )

struct ExactStringHashFunc
{
	uint operator()(const char *str) const
	{
		return reinterpret_cast<uint>(str);
	}

	int compare(const char* str1, const char* str2) const
	{
		return (str1 == str2) ? 0 : 1;
	}
};

struct StringHashFunc
{
	uint operator()(const char *str) const
	{
		int c;
		unsigned int hashval = 0;

		while ( (c = *str++) != 0 )
			hashval = hashval * 37 + tolower(c);

		return hashval;
	}

	int compare(const char* str1, const char* str2) const
	{
		return strcmpi(str1, str2);
	}
};

struct StringHashFuncCaseSensitive
{
	uint operator()(const char *str) const
	{
		int c;
		unsigned int hashval = 0;

		while ( (c = *str++) != 0 )
			hashval = hashval * 37 + c;

		return hashval;
	}

	int compare(const char* str1, const char* str2) const
	{
		return strcmp(str1, str2);
	}
};

template<class T> struct DirectHashFunc
{
	uint operator()(const T& val) const
	{
		return (uint)val;
	}

	int compare(const T& str1, const T& str2) const
	{
		if(str1 < str2)
			return -1;
		else if(str1 > str2)
			return 1;
		return 0;
	}
};


const int ENF_HASHMAP_SIZE = 8;

//-----------------------------------------------------------------------------
/*!
	HashMap is a Hashed Associative Container that associates objects of type
	Key with objects of type Data. It is also a Unique Associative Container, 
	meaning that no two elements have keys that compare equal using 
	bool Key::operator==(const Key&).
	HashFunc is a class with a const function operator that converts Key objects 
	into uintegers.
	<code>
		struct HashFunc {uint operator()(const Key *) const; };
	</code>
*/
template <class Key, class Data, class HashFunc = StringHashFunc> class HashMap
{
	struct Node
	{
		enum { EMPTY, OCCUPIED, REMOVED } m_Status;
		Key	m_Key;
		Data	m_Data;
		Node() : m_Status(EMPTY), m_Key(), m_Data() {}
	};

public:
	ENF_DECLARE_MMOBJECT(HashMap);

	ENF_INLINE void Swap(HashMap<Key, Data, HashFunc>& hash)
	{
		enf_swap(m_Table, hash.m_Table);
		enf_swap(m_Size, hash.m_Size);
		enf_swap(m_Removed, hash.m_Removed);
		enf_swap(m_Elements, hash.m_Elements);
	}

	/*!
		Creates a hashmap with initial size ENF_HASHMAP_SIZE
	*/
	HashMap() : m_Table(NULL), m_Size(0), m_Removed(0), m_Elements(0)
	{
	}
	
	/*!
		Constructor with initial minimum capacity.
		The actual table size will be the smallest power-of-two 
		greater than or equal to 2 * _minimumCapacity.
	*/
	HashMap(uint _minimumCapacity) : m_Size(0), m_Elements(0), m_Removed(0), m_Table(NULL)
	{
		for (m_Size = 4; m_Size < 2 * _minimumCapacity && m_Size < 0x80000000; m_Size <<= 1);
		m_Table = (Node*) GMemFunctions->New(m_Size * sizeof(Node), __FILE__, __LINE__);
		Assert(m_Table != NULL);

		NewObjects<Node>(m_Table, m_Size);
	}

	~HashMap()
	{
		if (m_Table)
		{
			DeleteObjects<Node>(m_Table, m_Size);
			GMemFunctions->Delete(m_Table);
			m_Table = NULL;
		}
	}

	/*!
	Insert new element into hash map.
		
	\param _key
	Key of element to be inserted.
	\param _data
	Data of element to be inserted.
	*/
	bool Insert(const Key& _key, const Data& _data)
	{
		//make sure we have room in the table
		EnsureCapacity();

		//initial hash position and probe increment
		uint pos, delta;
		DoubleHash(_key, pos, delta);

		//find element, empty or removed
		while(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) != 0)
			pos = (pos + delta) % m_Size;

		// if element was already here return false
		if (m_Table[pos].m_Status == Node::OCCUPIED)
			return false;

		uint freePos = pos;

		// continue looking for the element until empty 
		while(m_Table[pos].m_Status != Node::EMPTY && !(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) == 0)) 
			pos = (pos + delta) % m_Size;
		
		// if element was already here return false
		if (m_Table[pos].m_Status == Node::OCCUPIED)
			return false;

		//insert element
		Node *n = &m_Table[freePos];
	
		// decrease removed counter if the node was a removed one
		if (n->m_Status == Node::REMOVED) 
			m_Removed--;
	
		n->m_Status = Node::OCCUPIED;
		n->m_Key = _key;
		n->m_Data = _data;
	
		//increment element counter
		m_Elements++;

		// return true to indicate that a new element was inserted
		return true;
	}
	
	/*!
	Remove an element from hash map.
		
	\param _key
	Key of element to be removed.
	*/
	bool Remove(const Key & _key)
	{
		if (m_Elements == 0) return false;
		
		//initial hash position and probe increment
		uint pos, delta;
		DoubleHash(_key, pos, delta);

		//find element or empty
		while(m_Table[pos].m_Status != Node::EMPTY && !(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) == 0)) 
			pos = (pos + delta) % m_Size;
		
		//was element found?
		if (m_Table[pos].m_Status == Node::OCCUPIED)
		{
			// mark node as deleted
			m_Table[pos].m_Status = Node::REMOVED;
			// update counters		
			m_Elements--;
			m_Removed++;
			//indicate success
			return true;
		}

		//indicate failure
		return false;
	}

	/*!
	Search for an key in the hash map.
		
	\param _key
	The key of the element to find
	\return
	True if found, false otherwise
	*/
	bool Contains(const Key& _key) const
	{	
		if(m_Elements == 0) return false;

		//initial hash position and probe increment
		uint pos, delta;
		DoubleHash(_key, pos, delta);

		//find element or NULL
		while(m_Table[pos].m_Status != Node::EMPTY && !(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) == 0)) 
			pos = (pos + delta) % m_Size;

		//was element found?
		return (m_Table[pos].m_Status == Node::OCCUPIED);
	}

	/*!
		Search for an element with the given key.
			
		\param _key
		The key of the element to find
		\return
		Pointer to element data if found, NULL otherwise.
	*/
	Data Find(const Key& _key) const
	{	
		if (m_Elements == 0) return NULL;

		//initial hash position and probe increment
		uint pos, delta;
		DoubleHash(_key, pos, delta);

		//find element or NULL
		while(m_Table[pos].m_Status != Node::EMPTY && !(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) == 0)) 
			pos = (pos + delta) % m_Size;

		//return element if found
		return (m_Table[pos].m_Status == Node::OCCUPIED) ? m_Table[pos].m_Data : NULL;
	}

	bool Find(const Key& _key, Data& _data) const
	{	
		if (m_Elements == 0) return false;

		//initial hash position and probe increment
		uint pos, delta;
		DoubleHash(_key, pos, delta);

		//find element or NULL
		while(m_Table[pos].m_Status != Node::EMPTY && !(m_Table[pos].m_Status == Node::OCCUPIED && m_Hash.compare(m_Table[pos].m_Key, _key) == 0)) 
			pos = (pos + delta) % m_Size;

		//return element if found
		if(m_Table[pos].m_Status == Node::OCCUPIED)
		{
			_data = m_Table[pos].m_Data;
			return true;
		}
		return false;
	}

	/*!
	Return the i:th key in the map.
	Note: This operation is O(n) complexity. Use with care!

	\param i
	The position of the key in the map
	\return
	The key on the i:th position
	*/
	const Key& GetKey(uint i)
	{
		uint c = 0;
		for(uint j = 0; j < m_Size; j++)
		{
			if (m_Table[j].m_Status == Node::OCCUPIED)
			{
				if (c++ == i) return m_Table[j].m_Key;
			}
		}
		return NULL;
	}

	/*!
	Return the i:th element in the map.
	Note: This operation is O(n) complexity. Use with care!

	\param i
	The position of the element in the map
	\return
	The element on the i:th position
	*/
	const Data& GetElement(uint i)
	{
		uint c = 0;
		for(uint j = 0; j < m_Size; j++)
		{
			if (m_Table[j].m_Status == Node::OCCUPIED)
			{
				if (c++ == i) return m_Table[j].m_Data;
			}
		}
		return NULL;
	}

	/*!
		\return
		The number of elements in the hashmap.
	*/
	inline uint GetCardinality() const
	{
		return m_Elements;
	}

	/*!
		Clears the hash map.
	*/
	void Clear()
	{
		for(uint i = 0; i < m_Size; i++)
			m_Table[i].m_Status = Node::EMPTY;
		
		m_Elements = 0;
		m_Removed = 0;
	}

	/*!
		Rehash the hash map.
	*/
	void Rehash()
	{
		// store old table
		Node* oldTable = m_Table;

		//allocate table
		m_Table = (Node*)GMemFunctions->New(m_Size * sizeof(Node), __FILE__, __LINE__);
		Assert(m_Table != NULL);
		m_Removed = 0;

		uint i;

		for(i = 0; i < m_Size; i++)	
		{
			Node* tmp = m_Table + i;
			new ((void*)(tmp)) Node;
		}
		
		//rehash
		for(i = 0; i < m_Size; i++)
		{
			if (oldTable[i].m_Status == Node::OCCUPIED)
			{
				//initial hash position and probe increment
				uint pos;
				uint delta;
				DoubleHash(oldTable[i].m_Key, pos, delta);

				//find element, empty or removed
				while(m_Table[pos].m_Status == Node::OCCUPIED)
					pos = (pos + delta) % m_Size;
				
				m_Table[pos].m_Status = Node::OCCUPIED;
				m_Table[pos].m_Key = oldTable[i].m_Key;
				m_Table[pos].m_Data = oldTable[i].m_Data;
			}
		}
		
		// free memory for old table
		for(i = 0;i < m_Size; i++)
		{
			(oldTable+i)->~Node();
		}
		GMemFunctions->Delete(oldTable);
	}

	/// returns table size
	uint GetTableSize() const
	{
		return m_Size;
	}

	/// returns data at given table node
	const Data& GetTableNodeData(uint index) 
	{
		Assert(index < m_Size);
		if (index < m_Size && m_Table[index].m_Status == Node::OCCUPIED)
			return m_Table[index].m_Data;
		else
			return NULL;
	}

	HashMap& operator=(const HashMap& map)
	{
		Clear();
		for(uint n = 0; n < map.m_Size; n++)
		{
			if(map.m_Table[n].m_Status == Node::OCCUPIED)
				Insert(map.m_Table[n].m_Key, map.m_Table[n].m_Data);
		}
		return *this;
	}

	void Reserve(uint minimumCapacity)
	{
		uint newSize;
		for (newSize = 4; newSize < 2 * minimumCapacity && newSize < 0x80000000; newSize <<= 1);

		if(m_Size < newSize)
		{
			Resize(newSize);
		}
	}

protected:
	Node*		m_Table;
	uint		m_Size;
	// the number of nodes marked as deleted
	uint		m_Removed;
	// the number of nodes actually used
	uint		m_Elements;
	HashFunc	m_Hash;

	inline void DoubleHash(const Key& _key, uint& h, uint& p) const
	{
		uint hash = m_Hash(_key);
		h = hash % m_Size;
		p = (2 * hash + 1) % m_Size;
	}

	void Resize(uint newSize)
	{
		uint oldSize = m_Size;
		Node* oldTable = m_Table;

		// allocate the new table
		m_Table = (Node*)GMemFunctions->New(newSize * sizeof(Node), __FILE__, __LINE__);
		m_Size = newSize;
		m_Removed = 0;

		NewObjects(m_Table, newSize);

		// rehash
		for(uint i = 0; i < oldSize; i++)
		{
			if (oldTable[i].m_Status == Node::OCCUPIED)
			{	
				//initial hash position and probe increment
				uint pos, delta;
				DoubleHash(oldTable[i].m_Key, pos, delta);

				//find element, empty or removed
				while(m_Table[pos].m_Status == Node::OCCUPIED)
					pos = (pos + delta) % newSize;
				m_Table[pos].m_Status = Node::OCCUPIED;
				m_Table[pos].m_Key = oldTable[i].m_Key;
				m_Table[pos].m_Data = oldTable[i].m_Data;
			}
		}

		if(oldTable)
		{
			// free memory for old table				
			DeleteObjects(oldTable, oldSize);
			GMemFunctions->Delete((void*)oldTable);
		}
	}

	void EnsureCapacity()
	{
		uint oldSize = m_Size;

		if (m_Elements + m_Removed >= oldSize / 2)
		{
			// determine the new size
			uint newSize = (m_Elements >= oldSize / 2) ? enf_max(oldSize * 2, 8u) : oldSize;
			Resize(newSize);
		}
	}
};
//@}
}
#endif //ENF_HASHMAP
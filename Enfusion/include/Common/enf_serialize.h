//-----------------------------------------------------------------------------
// File: enf_serialize.h
//
// Desc: Serialize adapters
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_SERIALIZE_H
#define ENF_SERIALIZE_H

#include "enf_config.h"
#include "common/enf_types.h"
#include "common/enf_mathlib.h"
#include "common/enf_set.h"
#include "common/enf_strings.h"

//MUST be here. Otherwise we cannot play retail/dev version on the same machine :(
//#ifdef ENF_DEVELOPER
	#define ENF_VALIDATE_SERIALIZED
//#endif

#define ENF_SERIALIZE_VERSION 3
#define ENF_SERIALIZE_SUBVERSION 1

namespace enf
{
/** @addtogroup Common
@{*/

typedef uint SerializedIdentifier;
class StaticBuffer;
/*
class Matrix43;
class Matrix33;
class Vector3;
*/
class Matrix44;
class Vector4;
class Vector2;
class AAB;
class COutputStream;
class CInputStream;

class PUBLIC_API SerializeAdapter
{
public:
	//!Item in Table Of Content.
	struct TOCItem
	{
		//!Unique identifier of object. FourCC or something else.
		uint				m_iType;
		//!Additional info about object, represented by string (stored in constant string table). 
		uint				m_iClass;
		//!Original pointer of object
		const void*		m_pPointer;
		//!Offset in serialized stream.
		uint				m_iOffset;
		//!Size in serialized stream.
		uint				m_iSize;
		//!Set when it's really stored (not just referenced)
		bool				m_bStored;

		TOCItem() {}

		uint GetEndOffset() const
		{
			return m_iOffset + m_iSize;
		}

		TOCItem(uint type, const void* ptr):
			m_iType(type),
			m_iClass(0xffffffff),
			m_pPointer(ptr),
			m_iOffset(0),
			m_iSize(0),
			m_bStored(false)
		{
		}
	};

protected:
	//!Header of serialized stream.
	struct Header
	{
		uint	ID;
		uint	Version;
		uint	StringsOffset;
		uint	StringsSize;
		uint	TOCOffset;
		uint	TOCSize;
		char	Description[128];
		Header(uint id, int ver, int subver);
		Header()	{}
	};

	typedef	Pair<const void*, uint> TOCItemElement;

	//!Set of object pointers + index to m_TOC array.
	Set<TOCItemElement>	m_ItemsSet;

	//!Table of content.
	Array<TOCItem>			m_TOC;

	//!Array of constant strings. Used for mapping string->index
	Array<const char*>	m_Strings;

	const TOCItem* FindTOCItem(const void* ptr) const;
	TOCItem* FindTOCItem(const void* ptr);
	uint FindTOCItemIndex(const void* ptr) const;

	ENF_INLINE const char* GetTypeName(uint index) const
	{
		if(index == INDEX_NOT_FOUND)
			return "NULL";

		enf_assert(index < m_Strings.GetCardinality());

		return m_Strings[index];
	}

public:
	ENF_INLINE const char* GetString(uint index) const
	{
		enf_assert(index < m_Strings.GetCardinality());

		return m_Strings[index];
	}

	/*!
	This ask whether is this object already stored.
	\param ptr	Pointer to object we are asking for
	\return	True when object is stored, false otherwise.
	*/
	bool IsStored(const void* ptr) const;

	/*!
	This ask whether this object has been referenced.
	\param ptr	Pointer to object we are asking for
	\return	True when object has been referenced, false otherwise.
	*/
	bool IsReferenced(const void* ptr) const;

	SerializeAdapter()
	{
	}
};

class PUBLIC_API SerializeOutputAdapter: public SerializeAdapter
{
protected:
	static const uint MAX_NESTED_DEPTH = 32;

	COutputStream*	m_pStream;
	StaticBuffer*	m_pStatic;
	//!Nesting counter. Used only for validation ATM.
	int				m_iDepth;

	Header			m_Header;

	typedef	Pair<const char*, uint> StringElement;

	//!Set of constant strings + index to m_Strings array.
	Set<StringElement>	m_StringsSet;

	Array<uint>		m_StoredObjects;

	void WriteIdentifier(SerializedIdentifier ident);

#ifdef ENF_VALIDATE_SERIALIZED
	void WriteValidator(const uint& t);
#endif

	uint		m_StoreStack[MAX_NESTED_DEPTH];

public:
	ENF_INLINE COutputStream& GetStream(){return *m_pStream;}

	uint AddString(const char* str);

	/*!
	Some additional info, saved in head of stream
	\param str	String with description
	*/
	void SetDescription(CStr str);

	/*!
	Sets static pool where adapter can look for static strings. If he found the string
	there, only once is stored and only references to this string are used then.
	\param stat	Pointer to static buffer
	*/
	void SetStaticBuffer(StaticBuffer* stat)
	{
		m_pStatic = stat;
	}

	/*!
	Creates output serialization adapter.
	\param id	Some identifier of data. Could be FourCC.  Will be validated upon deserialization again.
	\param ver	Version of stream. Will be validated upon deserialization again.
	\param subver	Subversion of stream. Will be validated upon deserialization again.
	\param ostream	Pointer to output stream
	*/
	SerializeOutputAdapter(uint id, int ver, int subver, COutputStream* ostream);

	/*!
	Must be called upon storing pointer to some object regardless he is already serialized or not.
	\param type	Unique identifier of object. Could be FourCC for example
	\param ptr	Pointer to object
	\return	Unique identifier of serialized item
	*/
	SerializedIdentifier AddObjectReference(uint type, const void* ptr);

	/*!
	Must be called upon object serialization begin
	\param type	Unique identifier of object. Could be FourCC for example
	\param classname	Additional information about object
	\param ptr	Pointer to object
	\return	Unique identifier of serialized item
	*/
	SerializedIdentifier BeginObject(uint type, CStr classname, const void* ptr);
	
	/*!
	Must be called upon object serialization end.
	Object serialization cannot be nested ATM.
	*/
	void EndObject();

	/*!
	Add some generic data to stream
	\param data	Pointer to data
	\param length	Length of stored data
	*/
	void WriteData(const ubyte* data, uint length);
	/*!
	Add string to stream. Static strings are handled if some StaticBuffer has been assigned.
	\param str	Pointer to string
	*/
	void WriteString(CStr str);
	void WriteDString(const DString& str);
	void WriteUnicodeString(const wchar_t* str);
	void WritePointer(const void* ptr);
	void WriteBool(const bool& value);
	void WriteInt8(const char& value);
	void WriteUInt8(const ubyte& value);
	void WriteInt16(const short& value);
	void WriteUInt16(const ushort& value);
	void WriteInt32(const int& value);
	void WriteUInt32(const uint& value);
	void WriteFloat(const float& value);
	void WriteDouble(const double& value);
	void WriteVector2(const Vector2& value);
	void WriteVector3(const Vector3& value);
	void WriteVector4(const Vector4& value);
	void WriteMatrix33(const Matrix33& value);
	void WriteMatrix43(const Matrix43& value);
	void WriteMatrix44(const Matrix44& value);
	void WriteAAB(const AAB& value);
	/*!
	Internally this calls AddObjectIdentifier and result is passed to WriteIndetifier
	\param type	Type identifier
	\param ptr	Pointer to object
	*/
	void WriteReference(uint type, const void* ptr);
	
	/*!
	For non-binary adapters, here you can put some readable note into stream
	\param text	Commentary
	*/
	void WriteComment(const char* text);

	//!Close stream and write strings, TOC, head and so on.
	void Close();
};

class PUBLIC_API SerializeInputAdapter: public SerializeAdapter
{
	CInputStream*	m_pStream;
	StaticBuffer*	m_pStatic;
	//!Nesting counter. Used only for validation ATM.
	int				m_iDepth;

	Header			m_Header;
	bool				m_bValid;
	bool				m_bDataError;

	//!Array of restored created objects
	CArray<void*>	m_ObjectPointers;

	//index of current restored item. Used for skipping nested objects
	uint				m_iCurrentItem;

protected:
	/*!
	Called upon beginning of deserialization, when all objects must be created first, to
	resolve saved/new pointers.

	\param identifier	Original pointer of object.
	\param type	Unique type of object.
	\param classname	Additional info about object.
	\return	Pointer of new created object
	*/
	virtual void* CreateInstance(const void* identifier, uint type, CStr classname)=0;
	virtual bool RestoreObject(void* instanceptr, uint type, CStr classname)=0;

	virtual void* UserPointerCallback(void* instanceptr, uint type, CStr classname, void* userpointer)
	{
		return userpointer;
	}

	ENF_INLINE void SetObjectPointer(uint n, void* ptr)
	{
		enf_assert(ptr);
		enf_assert(m_ObjectPointers[n] == NULL);
		m_ObjectPointers[n] = ptr;
	}

	ENF_INLINE const Array<TOCItem>& GetTOC() const
	{
		return m_TOC;
	}

	SerializedIdentifier ReadIdentifier();

#ifdef ENF_VALIDATE_SERIALIZED
	void ReadValidator(uint t);
#endif

	/*!
	Kind of hack fo resolving issue with Meqon. He has his own serialization and we get real pointer to object
	at later stage (in RestoreObject method). We need to replace 'old' pointer here. This assumes that the
	replacement happens _before_anyone_ will reference these objects!
	\param identifier	old pointer
	\param pointer	new pointer
	*/
	void ReplaceInstancePointer(const void* identifier, void *pointer);

	void* GetInstancePointer(const void* identifier);
	void AdjustOffset();
	bool CheckEnd(uint item);

public:
	//ENF_INLINE CInputStream& GetStream(){return *m_pStream;}

	const char* GetString(uint index)
	{
		if(index < m_Strings.GetCardinality())
			return m_Strings[index];

		return NULL;
	}

	/*!
	Get additional info, saved in head of stream
	\return	String with description
	*/
	CStr GetDescription() const;

	/*!
	Creates input serialization adapter.
	\param id	Some identifier of data. Could be FourCC.  Will be validated upon deserialization again.
	\param ver	Version of stream. Will be validated upon deserialization again.
	\param subver	Subversion of stream. Will be validated upon deserialization again.
	\param istream	Pointer to stream with data
	\param stat	Pointer to shared pool for constant strings.
	*/
	SerializeInputAdapter(uint id, int ver, int subver, CInputStream* istream, StaticBuffer* stat);

	virtual ~SerializeInputAdapter();

	//!Tells whether is it valid or not.
	bool IsValid() const
	{
		return m_bValid;
	}

	/*!
	Read some generic data to stream
	\param data	Pointer to data
	\param length	Length of stored data
	*/
	void ReadData(ubyte* data, uint length);
	/*!
	Get string from stream.
	\return	Pointer to string
	*/
	const char* ReadString();
	const wchar_t* ReadUnicodeString();
	void* ReadPointer();
	bool ReadBool();
	char ReadInt8();
	ubyte ReadUInt8();
	short ReadInt16();
	ushort ReadUInt16();
	int ReadInt32();
	uint ReadUInt32();
	float ReadFloat();
	double ReadDouble();
	Vector2 ReadVector2();
	Vector3 ReadVector3();
	Vector4 ReadVector4();
	Matrix43 ReadMatrix43();
	Matrix33 ReadMatrix33();
	Matrix44 ReadMatrix44();
	AAB ReadAAB();

	/*!
	It reads stored pointer from stream and translates it into pointer to newly created object.
	\param type	Type identifier, used for validation
	\return pointer to new object
	*/
	void* ReadReference(uint type);
	template< class T > T* ReadObject(uint type)
	{
		return reinterpret_cast< T* >(ReadReference(type));
	}

	/*!
	For non-binary adapters, here you can put some readable note into stream
	\param text	Commentary
	*/
	void ReadComment(const char* text);

	void Seek(size_t offset);
	bool CreateAllObjects();
	bool RestoreAllObjects();

	//!Close stream.
	void Close();
};
//@}
}
#endif

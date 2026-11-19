//-----------------------------------------------------------------------------
// File: enf_serialize.cpp
//
// Desc: Serialize adapters
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------
//#include "precomp.h"
#include "enf_main.h"
#include "common/enf_serialize.h"
#include "common/enf_streams.h"
#include "common/enf_mathlib.h"
#include "common/enf_mathlib2D.h"
#include "common/enf_staticbuffer.h"

#ifdef ENF_VALIDATE_SERIALIZED
//-----------------------------------------------------------------------------
#ifdef ENF_DEVELOPER
//#define DUMP_CONTENT
#endif


namespace enf
{
enum DataType
{
	DT_STRING,
	DT_UCSTRING,
	DT_POINTER,
	DT_BOOL,
	DT_INT8,
	DT_UINT8,
	DT_INT16,
	DT_UINT16,
	DT_INT32,
	DT_UINT32,
	DT_FLOAT,
	DT_DOUBLE,
	DT_VECTOR2,
	DT_VECTOR3,
	DT_VECTOR4,
	DT_MATRIX33,
	DT_MATRIX43,
	DT_MATRIX44,
	DT_AAB,
	DT_REFERENCE,
	DT_COMMENT
};
}
#endif

namespace enf
{

//-----------------------------------------------------------------------------
const char* Type2Text(uint type)
{
	static char text[32];
	static uint off = 0;

	if(off > 24)
		off = 0;

	if(type == 0)
	{
		text[off + 0] = ' ';
		text[off + 1] = ' ';
		text[off + 2] = ' ';
		text[off + 3] = ' ';
	}
	else
	{
		type = BE_INT(type);
		text[off + 0] = (type & 0xff000000) >> 24;
		text[off + 1] = (type & 0xff0000) >> 16;
		text[off + 2] = (type & 0xff00) >> 8;
		text[off + 3] = (type & 0xff);
	}
	text[off + 4] = 0;

	uint base = off;

	off += 5;

	return &text[base];
}

//-----------------------------------------------------------------------------
SerializeAdapter::Header::Header(uint id, int ver, int subver)
{
	ID = BE_INT(id);
	Version = (ver << 16) | subver;
	StringsOffset = 0;
	StringsSize = 0;
	TOCOffset = 0;
	TOCSize = 0;
}

//-----------------------------------------------------------------------------
uint SerializeAdapter::FindTOCItemIndex(const void* ptr) const
{
	int index = m_ItemsSet.GetIndexOf(TOCItemElement(ptr));

	if(index == -1)
	{
		return INDEX_NOT_FOUND;
	}

	return m_ItemsSet[index].Data;
}

//-----------------------------------------------------------------------------
const SerializeAdapter::TOCItem* SerializeAdapter::FindTOCItem(const void* ptr) const
{
	uint index = FindTOCItemIndex(ptr);

	if(index == INDEX_NOT_FOUND)
	{
		return NULL;
	}

	return &m_TOC[index];
}

//-----------------------------------------------------------------------------
SerializeAdapter::TOCItem* SerializeAdapter::FindTOCItem(const void* ptr)
{
	uint index = FindTOCItemIndex(ptr);

	if(index == INDEX_NOT_FOUND)
	{
		return NULL;
	}

	return &m_TOC[index];
}

//-----------------------------------------------------------------------------
bool SerializeAdapter::IsReferenced(const void* ptr) const
{
	const TOCItem* item = FindTOCItem(ptr);

	if(item)
		return true;
	
	return false;
}

//-----------------------------------------------------------------------------
bool SerializeAdapter::IsStored(const void* ptr) const
{
	const TOCItem* item = FindTOCItem(ptr);

	if(item && item->m_bStored)
		return true;
	
	return false;
}

//-----------------------------------------------------------------------------
SerializeOutputAdapter::SerializeOutputAdapter(uint id, int ver, int subver, COutputStream* ostream): m_Header(id, ver, subver)
{
	enf_assert(ostream);
	m_iDepth = 0;
	m_pStream = ostream;
	m_pStatic = NULL;

	m_pStream->Write((const ubyte*)&m_Header, sizeof(m_Header));

}

//-----------------------------------------------------------------------------
uint SerializeOutputAdapter::AddString(const char* str)
{
	int index = m_StringsSet.GetIndexOf(StringElement(str));

	//is it already present?
	if(index == -1)
	{
		index = m_Strings.Insert(str);
		m_StringsSet.Insert(StringElement(str, index)); //no, add it
		return index;
	}
	else
		return m_StringsSet[index].Data;
}

//-----------------------------------------------------------------------------
SerializedIdentifier SerializeOutputAdapter::AddObjectReference(uint type, const void* ptr)
{
	if(ptr != NULL)
	{
		uint index = m_ItemsSet.GetIndexOf(TOCItemElement(ptr));

		if(index == INDEX_NOT_FOUND)
		{
			index = m_TOC.Insert(TOCItem(type, ptr));
			m_ItemsSet.Insert(TOCItemElement(ptr, index));
			return (SerializedIdentifier)ptr;
		}
		else
		{
			if(type != 0)
			{
				if(m_TOC[m_ItemsSet[index].Data].m_iType == 0)
				{
					//fix type
					m_TOC[m_ItemsSet[index].Data].m_iType = type;
				}
				else
				{
					enf_assert(m_TOC[m_ItemsSet[index].Data].m_iType == type);
				}
			}
		}
	}

	return (SerializedIdentifier)ptr;
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::EndObject()
{
	enf_assert(m_iDepth > 0);
	m_iDepth--;

	TOCItem& titem = m_TOC[m_StoreStack[m_iDepth]];

	titem.m_iSize = m_pStream->GetCurrentPosition() - titem.m_iOffset;
}

//-----------------------------------------------------------------------------
SerializedIdentifier SerializeOutputAdapter::BeginObject(uint type, CStr classname, const void* ptr)
{
	enf_assert(m_iDepth < 32);

	uint itemindex = FindTOCItemIndex(ptr);

	TOCItem* titem = itemindex != INDEX_NOT_FOUND ? &m_TOC[itemindex] : NULL;

	if(titem == NULL)
	{
		AddObjectReference(type, ptr);

		itemindex = FindTOCItemIndex(ptr);
		titem = &m_TOC[itemindex];
	}

	enf_assert(titem->m_bStored == false);
	enf_assert(titem->m_iType == type);

	titem->m_iClass = classname == NULL ? INDEX_NOT_FOUND : AddString(classname.cstr());
	titem->m_bStored = true;
	titem->m_iOffset = m_pStream->GetCurrentPosition();

	m_StoredObjects.Insert(itemindex);

	m_StoreStack[m_iDepth++] = itemindex;

#ifdef ENF_VALIDATE_SERIALIZED
	//just for validation
	WriteUInt32(BE_INT(type));
#endif
	return (SerializedIdentifier)ptr;
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteData(const ubyte* data, uint length)
{
	m_pStream->Write((const ubyte*)data, length);
}

//-----------------------------------------------------------------------------
#ifdef ENF_VALIDATE_SERIALIZED
void SerializeOutputAdapter::WriteValidator(const uint& value)
{
	ubyte nvalue = (ubyte)value;
	WriteData(&nvalue, sizeof(nvalue));
}
#endif	

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteString(CStr str)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_STRING);
#endif	
	if(str == NULL)
	{
		//mark it as special case. It's not the same as zero length string!
		WriteUInt32(INDEX_NOT_FOUND);
		return;
	}
	
	uint len = str.Length();

	//is it string from static heap?
	if(m_pStatic && m_pStatic->IsAllocated(str.cstr()))
	{
		const char* cstr = m_pStatic->FindString(str.cstr());

		//yes it is. Store it
		if(cstr)
		{
			uint index = AddString(cstr);
			WriteUInt32(index | 0x80000000);
			return;
		}
	}

	WriteUInt32(len);
	//don't fuck around with none data :)
	if(len)
	{
		WriteData((const ubyte*)str.cstr(), len);
	}
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteDString(const DString& str)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_STRING);
#endif	

	uint len = str.Length();

	WriteUInt32(len);
	//don't fuck around with none data :)
	if(len)
	{
		WriteData((const ubyte*)str.cstr(), len);
	}
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteUnicodeString(const wchar_t* str)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_UCSTRING);
#endif	
	if(str == NULL)
	{
		//mark it as special case. It's not the same as zero length string!
		WriteUInt32(INDEX_NOT_FOUND);
		return;
	}
	
	uint len = estrlen(str);

	WriteUInt32(len);

	wchar_t* sstr = ALLOCA(wchar_t,len);

	for(uint n = 0; n < len; n++)
	{
		sstr[n] = LE_SHORT(str[n]);
	}
	WriteData((const ubyte*)sstr, len * sizeof(wchar_t));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteInt8(const char& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_INT8);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteUInt8(const ubyte& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_UINT8);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteInt16(const short& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_INT16);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteUInt16(const ushort& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_UINT16);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteInt32(const int& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_INT32);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteUInt32(const uint& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_UINT32);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WritePointer(const void* value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_POINTER);
#endif	
	//FIXME: not correct. What about 64bit platforms?
	WriteUInt32((uint)value);
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteFloat(const float& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_FLOAT);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteVector2(const Vector2& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_VECTOR2);
#endif	
	//OPTIMIZE - write it at once
	WriteData((const ubyte*)value.v, sizeof(value.v));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteVector3(const Vector3& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_VECTOR3);
#endif	
	//OPTIMIZE - write it at once
	WriteData((const ubyte*)value.v, sizeof(value.v));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteVector4(const Vector4& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_VECTOR4);
#endif	
	//OPTIMIZE - write it at once
	WriteData((const ubyte*)&value.v, sizeof(value.v));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteMatrix33(const Matrix33& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_MATRIX33);
#endif
	float mat[3][3];
	for(uint c = 0; c < 3; c++)
	{
    mat[0][c] = value.Direction().Get(c);
    mat[1][c] = value.DirectionUp().Get(c);
    mat[2][c] = value.DirectionAside().Get(c);
	}
	WriteData((const ubyte*)mat, sizeof(mat));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteMatrix43(const Matrix43& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_MATRIX43);
#endif
	float mat[4][3];
  for(uint c = 0; c < 3; c++)
  {
    mat[0][c] = value.Direction().Get(c);
    mat[1][c] = value.DirectionUp().Get(c);
    mat[2][c] = value.DirectionAside().Get(c);
    mat[3][c] = value.Position().Get(c);
  }
	WriteData((const ubyte*)mat, sizeof(mat));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteMatrix44(const Matrix44& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_MATRIX44);
#endif	
	float mat[4][4];
	for(uint r = 0; r < 4; r++)
	{
		for(uint c = 0; c < 4; c++)
			mat[r][c] = value.m[r][c];
	}
	WriteData((const ubyte*)mat, sizeof(mat));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteAAB(const AAB& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_AAB);
#endif
	float bb[2][3];
	bb[0][0] = value.GetMins().x;
	bb[0][1] = value.GetMins().y;
	bb[0][2] = value.GetMins().z;
	bb[1][0] = value.GetMaxs().x;
	bb[1][1] = value.GetMaxs().y;
	bb[1][2] = value.GetMaxs().z;
	WriteData((const ubyte*)bb, sizeof(bb));
}


//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteBool(const bool& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_BOOL);
#endif	
	ubyte byte = value ? 1 : 0;

	WriteData(&byte, 1);
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteDouble(const double& value)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_DOUBLE);
#endif	
	WriteData((const ubyte*)&value, sizeof(value));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteIdentifier(SerializedIdentifier ident)
{
	enf_assert(sizeof(SerializedIdentifier) == sizeof(uint));
	WriteUInt32(ident);
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::WriteReference(uint type, const void* ptr)
{
#ifdef ENF_VALIDATE_SERIALIZED
	WriteValidator(DT_REFERENCE);
#endif	
	WriteIdentifier(AddObjectReference(type, ptr));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::Close()
{
	m_Header.TOCOffset = m_pStream->GetCurrentPosition();

	//check TOC
#if defined(ENF_BUILD)
	for(uint n = 0; n < m_TOC.GetCardinality(); n++)
	{
		TOCItem* titem = &m_TOC[n];
		if(titem->m_bStored)
			continue;
		uint type = titem->m_iType;

		LogF(LL_NORMAL, "Object is not stored. Pointer %x, type '%s', class %s",
			titem->m_pPointer,
			Type2Text(type),
			GetTypeName(titem->m_iClass));
		type=type;
	}
#endif

	WriteUInt32(m_StoredObjects.GetCardinality());
	for(uint n = 0; n < m_StoredObjects.GetCardinality(); n++)
	{
		TOCItem* titem = &m_TOC[m_StoredObjects[n]];

		WriteUInt32(BE_INT(titem->m_iType));
		WriteUInt32(titem->m_iClass);
		WritePointer(titem->m_pPointer);
		WriteUInt32(titem->m_iOffset);
		WriteUInt32(titem->m_iSize);
	}
	m_Header.TOCSize = m_pStream->GetCurrentPosition() - m_Header.TOCOffset;

	//Write strings
	m_Header.StringsOffset = m_pStream->GetCurrentPosition();
	for(uint n = 0; n < m_Strings.GetCardinality(); n++)
	{
		WriteData((const ubyte*)m_Strings[n], strlen(m_Strings[n]) + 1);
	}
	m_Header.StringsSize = m_pStream->GetCurrentPosition() - m_Header.StringsOffset;

	//write header
	m_pStream->Seek(0);
	m_pStream->Write((const ubyte*)&m_Header, sizeof(m_Header));
}

//-----------------------------------------------------------------------------
void SerializeOutputAdapter::SetDescription(CStr str)
{
	strcpy(m_Header.Description, str.cstr());
}

//-----------------------------------------------------------------------------
CStr SerializeInputAdapter::GetDescription() const
{
	return m_Header.Description;
}

//-----------------------------------------------------------------------------
void SerializeInputAdapter::Seek(size_t offset)
{
	m_pStream->Seek(m_pStream->GetCurrentPosition() + offset);
}

//-----------------------------------------------------------------------------
void SerializeInputAdapter::AdjustOffset()
{
	//not deserializing
	if(m_iCurrentItem == INDEX_NOT_FOUND)
		return;

	//check for nested objects and skip them
	uint off = m_pStream->GetCurrentPosition();
	uint off2 = off;

	while(m_iCurrentItem < (m_TOC.GetCardinality() - 1))
	{
		enf_assert(off <= m_TOC[m_iCurrentItem + 1].m_iOffset);

		//no nested object. Break out
		if(off < m_TOC[m_iCurrentItem + 1].m_iOffset)
			break;

		//we encountered some nested objects. Skip them!
		off += m_TOC[++m_iCurrentItem].m_iSize;

		while(m_iCurrentItem < (m_TOC.GetCardinality() - 1) && off >= (m_TOC[m_iCurrentItem + 1].m_iOffset + m_TOC[m_iCurrentItem + 1].m_iSize))
			m_iCurrentItem++;
	}

	//we moved, seek stream pointer
	if(off != off2)
		m_pStream->Seek(off);
}

//-----------------------------------------------------------------------------
bool SerializeInputAdapter::CheckEnd(uint item)
{
	const TOCItem& citem = m_TOC[item];

	//check for nested objects and skip them
	uint off = m_pStream->GetCurrentPosition();
	uint off2 = off;

	while(item < (m_TOC.GetCardinality() - 1))
	{
		enf_assert(off <= m_TOC[item + 1].m_iOffset);

		//no nested object. Break out
		if(off <= m_TOC[item + 1].m_iOffset)
			break;

		//we encountered some nested objects. Skip them!
		off += m_TOC[++item].m_iSize;

		while(item < (m_TOC.GetCardinality() - 1) && off >= m_TOC[item + 1].GetEndOffset())
			item++;
	}

	if(off != (citem.GetEndOffset()))
	{
		const char* className = (citem.m_iClass == INDEX_NOT_FOUND) ? NULL : GetString(citem.m_iClass);
#if defined(ENF_VALIDATE_SERIALIZED) && defined(ENF_BUILD)
		LogF(LL_NORMAL, "Object is not read completely. Type '%s', Class %s", Type2Text(citem.m_iType), className);
#endif
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
void SerializeInputAdapter::ReadData(ubyte* data, uint length)
{
	AdjustOffset();
	if(m_pStream->Read((ubyte*)data, length) != length)
		m_bDataError = true;
}

//-----------------------------------------------------------------------------
#ifdef ENF_VALIDATE_SERIALIZED
void SerializeInputAdapter::ReadValidator(uint type)
{
	ubyte nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));

	enf_assert(nvalue == type);
}
#endif
//-----------------------------------------------------------------------------
const char* SerializeInputAdapter::ReadString()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_STRING); 
#endif

	uint length = ReadUInt32();
	if(length == INDEX_NOT_FOUND)
	{
		return NULL;
	}
	
	//is it string from static heap?
	if(length & 0x80000000)
	{
		enf_assert((length & 0x7fffffff) < m_Strings.GetCardinality());

		return m_Strings[length & 0x7fffffff];
	}

	static char temp[16384];

	enf_assert(length < 16384);
	if(length)
	{
		m_pStream->Read((ubyte*)temp, length);
	}
	temp[length] = '\0';
	return temp;
}

//-----------------------------------------------------------------------------
const wchar_t* SerializeInputAdapter::ReadUnicodeString()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_UCSTRING); 
#endif

	uint length = ReadUInt32();
	if(length == INDEX_NOT_FOUND)
	{
		return NULL;
	}
	
	enf_assert(length < 16384);
	
	static wchar_t temp[16384];

	if(length)
	{
		ReadData((ubyte*)temp, length * sizeof(wchar_t));
	}

	temp[length] = '\0';
	return temp;
}

//-----------------------------------------------------------------------------
char SerializeInputAdapter::ReadInt8()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_INT8); 
#endif
	char nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
ubyte SerializeInputAdapter::ReadUInt8()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_UINT8); 
#endif
	ubyte nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
short SerializeInputAdapter::ReadInt16()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_INT16); 
#endif
	short nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
ushort SerializeInputAdapter::ReadUInt16()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_UINT16); 
#endif
	ushort nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
int SerializeInputAdapter::ReadInt32()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_INT32); 
#endif
	int nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
uint SerializeInputAdapter::ReadUInt32()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_UINT32); 
#endif
	uint nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
void* SerializeInputAdapter::ReadPointer()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_POINTER); 
#endif
	//FIXME: not correct. What about 64bit platforms?
	return (void*)ReadUInt32();
}

//-----------------------------------------------------------------------------
float SerializeInputAdapter::ReadFloat()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_FLOAT); 
#endif

	float nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
Vector2 SerializeInputAdapter::ReadVector2()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_VECTOR2); 
#endif
	Vector2 vec;
	//OPTIMIZE - read it at once
	ReadData((ubyte*)&vec, sizeof(vec));

	return vec;
}

//-----------------------------------------------------------------------------
Vector3 SerializeInputAdapter::ReadVector3()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_VECTOR3); 
#endif
	Vector3 vec;
	//OPTIMIZE - read it at once
	ReadData((ubyte*)vec.v, sizeof(vec.v));

	return vec;
}

//-----------------------------------------------------------------------------
Vector4 SerializeInputAdapter::ReadVector4()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_VECTOR4); 
#endif
	Vector4 vec;
	//OPTIMIZE - read it at once
	ReadData((ubyte*)vec.v, sizeof(vec.v));
	return vec;
}

//-----------------------------------------------------------------------------
Matrix33 SerializeInputAdapter::ReadMatrix33()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_MATRIX33); 
#endif
	float mat[3][3];
	Matrix33 value;
	ReadData((ubyte*)mat, sizeof(mat));
  value.SetDirection(Vector3(mat[0][0], mat[0][1], mat[0][2]));
  value.SetDirectionUp(Vector3(mat[1][0], mat[1][1], mat[1][2]));
  value.SetDirectionAside(Vector3(mat[2][0], mat[2][1], mat[2][2]));

	return value;
}

//-----------------------------------------------------------------------------
Matrix43 SerializeInputAdapter::ReadMatrix43()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_MATRIX43); 
#endif
	float mat[4][3];
	Matrix43 value;
	ReadData((ubyte*)mat, sizeof(mat));
  value.SetDirection(Vector3(mat[0][0], mat[0][1], mat[0][2]));
  value.SetDirectionUp(Vector3(mat[1][0], mat[1][1], mat[1][2]));
  value.SetDirectionAside(Vector3(mat[2][0], mat[2][1], mat[2][2]));
  value.SetPosition(Vector3(mat[3][0], mat[3][1], mat[3][2]));

	return value;
}

//-----------------------------------------------------------------------------
Matrix44 SerializeInputAdapter::ReadMatrix44()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_MATRIX44); 
#endif
	float mat[4][4];
	Matrix44 value;
	ReadData((ubyte*)mat, sizeof(mat));
	for(uint r = 0; r < 4; r++)
	{
		value.SetRow(r, Vector4(mat[r]));
	}

	return value;
}

//-----------------------------------------------------------------------------
AAB SerializeInputAdapter::ReadAAB()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_AAB); 
#endif
	float bb[2][3];
	ReadData((ubyte*)bb, sizeof(bb));

	return AAB(bb[0][0], bb[0][1], bb[0][2], bb[1][0], bb[1][1], bb[1][2]);
}

//-----------------------------------------------------------------------------
bool SerializeInputAdapter::ReadBool()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_BOOL); 
#endif
	ubyte byte;

	ReadData(&byte, 1);

	return (byte == 1);
}

//-----------------------------------------------------------------------------
double SerializeInputAdapter::ReadDouble()
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_DOUBLE); 
#endif
	double nvalue;
	ReadData((ubyte*)&nvalue, sizeof(nvalue));
	return nvalue;
}

//-----------------------------------------------------------------------------
SerializedIdentifier SerializeInputAdapter::ReadIdentifier()
{
	enf_assert(sizeof(SerializedIdentifier) == sizeof(uint));

	return ReadUInt32();
}

//-----------------------------------------------------------------------------
void* SerializeInputAdapter::GetInstancePointer(const void* identifier)
{
	SerializedIdentifier ident = ReadIdentifier();

	//it's just stored NULL
	if(ident == 0)
		return NULL;

	uint index = FindTOCItemIndex((const void*)ident);

	if(index == INDEX_NOT_FOUND)
	{
		return NULL;
	}

	return m_ObjectPointers[index];
}

//-----------------------------------------------------------------------------
void* SerializeInputAdapter::ReadReference(uint type)
{
#ifdef ENF_VALIDATE_SERIALIZED
	ReadValidator(DT_REFERENCE); 
#endif
	SerializedIdentifier ident = ReadIdentifier();

	//it's just stored NULL
	if(ident == 0)
		return NULL;

	uint index = FindTOCItemIndex((const void*)ident);

	//strange, it's NOT in TOC???
	if(index == INDEX_NOT_FOUND)
	{
#if defined(ENF_VALIDATE_SERIALIZED) && defined(ENF_BUILD)
		LogF(LL_ERROR, "SerializeInputAdapter::ReadReference - Object not presented in TOC. Type '%s'", Type2Text(type));
#endif
		return NULL;
	}

#if defined(ENF_VALIDATE_SERIALIZED) && defined(ENF_BUILD)
	if(type != 0 && m_TOC[index].m_iType != type)
	{
		LogF(LL_NORMAL, "Referencing object as different type. Stored type '%s', referenced as '%s'",
			Type2Text(m_TOC[index].m_iType),
			Type2Text(type));
	}
#endif
	//isn't stored.
	if(!m_TOC[index].m_bStored)
	{
#if defined(ENF_VALIDATE_SERIALIZED) && defined(ENF_BUILD)
		LogF(LL_NORMAL, "SerializeInputAdapter::ReadReference - Not stored object. Type '%s', Class '%s'", Type2Text(type), GetTypeName(m_TOC[index].m_iClass));
#endif
		return NULL;
	}

	//is stored but unresolved
	if(m_ObjectPointers[index] == NULL)
	{
#if defined(ENF_VALIDATE_SERIALIZED) && defined(ENF_BUILD)
		LogF(LL_ERROR, "SerializeInputAdapter::ReadReference - Not created object. Type '%s', Class '%s'", Type2Text(type), GetTypeName(m_TOC[index].m_iClass));
#endif
		return NULL;
	}

	return m_ObjectPointers[index];
}

//-----------------------------------------------------------------------------
SerializeInputAdapter::~SerializeInputAdapter()
{
}

#ifdef DUMP_CONTENT
struct UniqueType
{
	uint type;
	uint className;
	uint count;
	uint size;

	int compare(const UniqueType& other) const
	{
		if(type < other.type) return -1;
		if(type > other.type) return 1;
		if(className < other.className) return -1;
		if(className > other.className) return 1;
		return 0;
	}
	bool operator>(const UniqueType& other) const { return compare(other) > 0; }
	bool operator<(const UniqueType& other) const { return compare(other) < 0; }
	bool operator==(const UniqueType& other) const { return compare(other) == 0; }

	UniqueType(): type(0), className(0), count(0), size(0) {}
	UniqueType(uint t, uint c, uint s): type(t), className(c), count(1), size(s) {}
};
#endif

SerializeInputAdapter::SerializeInputAdapter(uint id, int ver, int subver, CInputStream* istream, StaticBuffer* buff):
	m_Header(id, ver, subver),
	m_iDepth(0),
	m_pStream(istream),
	m_pStatic(buff),
	m_bValid(false)
{
	enf_assert(istream);
	enf_assert(m_pStatic);
	m_bDataError = false;

	//tell to ReadData that we are not restoring object ATM
	m_iCurrentItem = INDEX_NOT_FOUND;

	Header head;

	size_t size = m_pStream->GetSize();

	//check ID and version of header

	if(m_pStream->Read((ubyte*)&head, sizeof(head)) != sizeof(head) ||
		head.ID != m_Header.ID ||
		head.Version != m_Header.Version)
	{
		//bad one!
		return;
	}

	if(head.TOCOffset == 0)
	{
		//bad one. unfinished write probably
		return;
	}

	size_t dataOff = m_pStream->GetCurrentPosition();

	//read TOC table
	m_pStream->Seek(head.TOCOffset);

	uint items = ReadUInt32();

	//some nonsense...
	if(items > 0x100000)
		return;

	m_TOC.SetSize(items);
	m_ObjectPointers.SetSize(items);
	m_ItemsSet.Reserve(items);

	for(uint n = 0; n < items; n++)
	{
		m_TOC[n].m_iType = BE_INT(ReadUInt32());
		m_TOC[n].m_iClass = ReadUInt32();
		m_TOC[n].m_pPointer = ReadPointer();
		m_TOC[n].m_iOffset = ReadUInt32();
		if(m_TOC[n].m_iOffset >= size)
			return;
		m_TOC[n].m_iSize = ReadUInt32();
		if(m_TOC[n].m_iSize >= size)
			return;
		m_TOC[n].m_bStored = true;

		m_ObjectPointers[n] = NULL;
		//put it to hashtable too
		m_ItemsSet.Insert(TOCItemElement(m_TOC[n].m_pPointer, n));
	}

	if(m_bDataError)
		return;

	m_Header = head;

	//some nonsense
	if(head.StringsSize > 0x100000)
		return;

	//read constant strings, through small buffer, stack size is 64KB for PS3
	const uint BufferSize = 16384;
	char buffer[BufferSize];

	uint currSize = head.StringsSize;
	uint left = 0;

	while (currSize)
	{
		uint readBytes = enf_min(currSize, BufferSize - left);

		char* currBuffer = buffer + left;
		if(m_pStream->Read((ubyte*) currBuffer, readBytes) != readBytes)
			return;
		
		enf_assert((int) currSize - (int) readBytes >= 0);
		currSize -= readBytes;

		char* strings = buffer;
		char* end = currBuffer + readBytes;

		while (strings < end)
		{
			//skip to next string
			char* last = strings;

			while(*strings != 0 && strings < end)
				strings++;
			
			if (strings < end)
			{
				//add it to static heap
				m_Strings.Insert(m_pStatic->AddString(last));

				//skip '\0'
				strings++;
				left = 0;
			}
			else
			{
				//something left
				left = end - last;
				if (left)
				{
					//has not to overlap
					enf_assert(buffer + left < last);
					memcpy (buffer, last, left);
				}
			}
		}
	}
/*
	char* strings = ALLOCA(char,head.StringsSize);

	if(m_pStream->Read((ubyte*)strings, head.StringsSize) != head.StringsSize)
		return;

	char* end = strings + head.StringsSize;
	
	while(strings < end)
	{
		//add it to static heap
		m_Strings.Insert(m_pStatic->AddString(strings));
		
		//skip to next string
		while(*strings != 0 && strings < end)
			strings++;

		//skip '\0'
		strings++;
	}
*/

#ifdef DUMP_CONTENT
	Set<UniqueType> uniques(32);
	for(uint n = 0; n < items; n++)
	{
		UniqueType q(m_TOC[n].m_iType, m_TOC[n].m_iClass, m_TOC[n].m_iSize);
		uint index = uniques.GetIndexOf(q);
		if(index == INDEX_NOT_FOUND)
			uniques.Insert(q);
		else
		{
			uniques[index].count++;
			uniques[index].size += m_TOC[n].m_iSize;
		}
	}

	for(uint n = 0; n < uniques.GetCardinality(); n++)
	{
		LogF(LL_NORMAL, "type %s class %s count %d size %d", Type2Text(uniques[n].type), GetTypeName(uniques[n].className), uniques[n].count, uniques[n].size);
	}
#endif

	//seems to be OK
	m_bValid = true;

/*
	for(uint n = 0; n < items; n++)
	{
		LogF(LL_NORMAL, "Item type %x, class %s, ptr %x, offset %d, size %d, stored %d",
			m_TOC[n].m_iType, GetTypeName(m_TOC[n].m_iClass), m_TOC[n].m_pPointer, m_TOC[n].m_iOffset, m_TOC[n].m_iSize, (int)m_TOC[n].m_bStored);
	}
*/
	m_pStream->Seek(dataOff);
}

//-----------------------------------------------------------------------------
bool SerializeInputAdapter::CreateAllObjects()
{
	SString<> err;
	static const char* lastclassname = NULL;
	static uint lasttype = 0;

	const Array<SerializeAdapter::TOCItem>& items = GetTOC();

	for(uint n = 0; n < items.GetCardinality(); n++)
	{
		if(items[n].m_bStored)
		{
			lastclassname = (items[n].m_iClass == INDEX_NOT_FOUND) ? NULL : GetString(items[n].m_iClass);
			lasttype = items[n].m_iType;
			
			void* ptr = CreateInstance(items[n].m_pPointer, lasttype, lastclassname);

			if(ptr)
				SetObjectPointer(n, ptr);
		}


//#ifdef ENF_VALIDATE_SERIALIZED
//		uint type = items[n].m_iType;
//		LogF(LL_NORMAL, "Creating object. Type '%s', Class '%s', Pointer %x, New pointer %x", Type2Text(type), GetTypeName(items[n].m_iClass), items[n].m_pPointer, m_ObjectPointers[n]);
//#endif
	}

	return true;
}

//-----------------------------------------------------------------------------
void SerializeInputAdapter::ReplaceInstancePointer(const void* identifier, void* pointer)
{
	uint index = FindTOCItemIndex(identifier);

	if(index != INDEX_NOT_FOUND)
	{
		m_ObjectPointers[index] = pointer;
	}
}


//-----------------------------------------------------------------------------
bool SerializeInputAdapter::RestoreAllObjects()
{
	SString<> err;
	static const char* lastclassname = NULL;
	static uint lasttype = 0;

	m_pStream->Seek(sizeof(m_Header));

	//create all objects
	for(uint n = 0; n < m_TOC.GetCardinality(); n++)
	{
		if(!m_TOC[n].m_bStored)
			continue;

		m_pStream->Seek(m_TOC[n].m_iOffset);

		lastclassname = (m_TOC[n].m_iClass == INDEX_NOT_FOUND) ? NULL : GetString(m_TOC[n].m_iClass);
		lasttype = m_TOC[n].m_iType;
		m_iCurrentItem = n;

#ifdef ENF_VALIDATE_SERIALIZED
		uint vtype = BE_INT(ReadUInt32());
		enf_assert(vtype == lasttype);
#endif
		if(RestoreObject(m_ObjectPointers[n], lasttype, lastclassname))
		{
			
			//check position and sizes, if we read all data
//#ifdef ENF_VALIDATE_SERIALIZED
//			CheckEnd(n);
//#endif
		}
	}

	return true;
}

}
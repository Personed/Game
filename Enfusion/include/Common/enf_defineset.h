//-----------------------------------------------------------------------------
// File: enf_defineset.h
//
// Desc: 
//
// Copyright (c) 2000-2005 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_DEFINESET_H
#define ENF_DEFINESET_H

#include "enf_config.h"
#include "common/enf_types.h"
#include "common/enf_set.h"
#include "common/enf_strings.h"

namespace enf
{
//-----------------------------------------------------------------------------
//! for defining defines for effect/shader compiling
//-----------------------------------------------------------------------------
struct Define
{
	CStr	Name;		///< name of define
	CStr	Value;	///< value of define

	/*!
	operator ==
	\param def	second define
	\return true if two defines equals
	*/
	bool operator==(const Define& def) const
	{
		return (Name == def.Name && Value == def.Value);
	}

	/*!
	operator >
	\param def	second define
	\return true if this define is bigger than second (def)
	*/
	bool operator>(const Define& def) const
	{
		int cn = Name.cmp(def.Name);
		if(cn < 0) return false;
		if(cn > 0) return true;
		return (Value > def.Value);
	}

	/*!
	operator <
	\param def	second define
	\return true if this define is lesser than second (def)
	*/
	bool operator<(const Define& def) const
	{
		int cn = Name.cmp(def.Name);
		if(cn > 0) return false;
		if(cn < 0) return true;
		return (Value < def.Value);
	}

	//! empty constructor
	Define(): Name(NULL), Value(NULL) {}

	/*!
	constructor
	\param name		name of define
	\param value	value for define
	*/
	Define(CStr name, CStr value);
	/*!
	constructor
	\param name		name of define
	\param value	numeric value for define
	*/
	Define(CStr name, int value);
	/*!
	constructor
	\param name		name of define
	*/
	Define(CStr name);
};

class DefineSet: public Set<Define>
{
public:

	int Compare(const DefineSet& def2) const
	{
		if(GetCardinality() < def2.GetCardinality())
			return -1;
			
		if(GetCardinality() > def2.GetCardinality())
			return 1;

		for(uint c = 0; c < GetCardinality(); c++)
		{
			if(GetElement(c) > def2.GetElement(c))
				return 1;
			if(GetElement(c) < def2.GetElement(c))
				return -1;
		}
		return 0;
	}

	uint GetHashValue() const
	{
		uint hashval = 0;
		for(uint c = 0; c < GetCardinality(); c++)
		{
			hashval += (uint)GetElement(c).Name.cstr();
			hashval += (uint)GetElement(c).Value.cstr();
		}
		return hashval;
	}

	const char* Find(CStr define) const;

	//!Appends array of defines, terminated by NULL
	void Append(const char* const* defines);

};

//!Utility function to make string from number. Use it for shader/effect variants only!
const char* Num2Str(uint v);

//!Utility function to make string from bool. Use it for shader/effect variants only!
const char* Bool2Str(bool v);

}

#endif //ENF_DEFINESET_H
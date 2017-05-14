#pragma once

#include "tiny.h"

typedef struct Tiny_Object
{
	bool marked;

	Tiny_ValueType type;
	struct Tiny_Object* next;

	union
	{
		char* string;

		struct
		{
			void* addr;
			const Tiny_NativeProp* prop;	// Can be used to check type of native (ex. obj->nat.prop == &ArrayProp // this is an Array)
		} nat;
	};
} Tiny_Object;

typedef struct Tiny_Value
{
	Tiny_ValueType type;

	union
	{
		bool boolean;
		double number;
		Tiny_Object* obj;
	};
} Tiny_Value;
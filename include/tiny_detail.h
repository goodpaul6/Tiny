#pragma once

#include "tiny.h"

#define MAX_TOK_LEN		256
#define MAX_PROG_LEN	2048
#define MAX_STACK		256
#define MAX_INDIR		512
#define MAX_ARGS		32
#define MAX_NUMBERS     512
#define MAX_STRINGS     512

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

typedef unsigned char Word;

typedef enum
{
	SYM_GLOBAL,
	SYM_LOCAL,
    SYM_CONST,
	SYM_FUNCTION,
	SYM_FOREIGN_FUNCTION
} SymbolType;

typedef struct sSymbol
{
	struct sSymbol* next;
	SymbolType type;
	char* name;

	union
	{
		struct
		{
			bool initialized;	// Has the variable been assigned to?
			bool scopeEnded;	// If true, then this variable cannot be accessed anymore
			int scope, index;
		} var; // Used for both local and global

		struct
		{
            bool isString;  // true if it's a string, false if it's a number
			int index;
		} constant;

		struct
		{
			int index;
			int nargs, nlocals;

			struct sSymbol* argsHead;
			struct sSymbol* localsHead;
		} func;

		struct
		{
			Tiny_ForeignFunction callee;
			int index;
		} foreignFunc;
	};
} Symbol;

typedef struct Tiny_State
{
	// Program info
	Word program[MAX_PROG_LEN];
	int programLength;
    
    int numGlobalVars;

	int numFunctions;
	int* functionPcs;

	int numForeignFunctions;
	Tiny_ForeignFunction* foreignFunctions;

	// Compiler Info
    int currScope;
	Symbol* currFunc;
	Symbol* globalSymbols;

	const char* fileName;
	int lineNumber;
} Tiny_State;


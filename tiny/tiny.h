#ifndef TINY_H
#define TINY_H

#include <stdio.h>
#include <stdbool.h>

// Stores properties about a native
// object. This should be statically allocated
// and only one should exist for each type of
// Native value.
typedef struct
{
	const char* name;
	void(*mark)(void*);
	void(*free)(void*);
	void(*toString)(void*);
} Tiny_NativeProp;

typedef enum
{
	TINY_VAL_NULL,
	TINY_VAL_BOOL,
	TINY_VAL_NUM,
	TINY_VAL_STRING,
	TINY_VAL_NATIVE
} Tiny_ValueType;

typedef struct Tiny_Object Tiny_Object;
typedef struct Tiny_Value Tiny_Value;

typedef Tiny_Value (*Tiny_ForeignFunction)(const Tiny_Value* args, int count);

extern const Tiny_Value Tiny_Null;

void* emalloc(size_t size);
void* erealloc(void* mem, size_t newSize);
char* estrdup(const char* string);

void Tiny_Mark(Tiny_Object* obj);

Tiny_Value Tiny_NewBool(bool value);
Tiny_Value Tiny_NewNumber(double value);
Tiny_Value Tiny_NewString(char* string);
Tiny_Value Tiny_NewNative(void* ptr, const Tiny_NativeProp* prop);

Tiny_ValueType Tiny_GetType(const Tiny_Value val);

#define Tiny_IsNull(v) (Tiny_GetType(v) == TINY_VAL_NULL)
#define Tiny_IsObject(v) (Tiny_GetType(v) == TINY_VAL_STRING || Tiny_GetType(v) == TINY_VAL_NATIVE)

bool Tiny_GetBool(const Tiny_Value val, bool* pbool);
bool Tiny_GetNum(const Tiny_Value val, double* pnum);
bool Tiny_GetString(const Tiny_Value val, const char** pstr);
bool Tiny_GetAddr(const Tiny_Value val, void** paddr);
bool Tiny_GetProp(const Tiny_Value val, const Tiny_NativeProp** pprop);

bool Tiny_ExpectBool(const Tiny_Value val);
double Tiny_ExpectNum(const Tiny_Value val);
const char* Tiny_ExpectString(const Tiny_Value val);
void* Tiny_ExpectAddr(const Tiny_Value val);
const Tiny_NativeProp* Tiny_ExpectProp(const Tiny_Value val);

void Tiny_BindForeignFunction(Tiny_ForeignFunction func, const char* name);

void Tiny_DefineConstNumber(const char* name, double number);
void Tiny_DefineConstString(const char* name, const char* string);

void Tiny_ResetCompiler(void);
void Tiny_CompileFile(const char* filename, FILE* in);

void Tiny_ResetMachine(void);
void Tiny_RunMachine(void);

#endif
 

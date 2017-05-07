#ifndef TINY_H
#define TINY_H

#include <stdio.h>

struct sObject;

typedef enum
{
	OBJ_STRING,
	OBJ_NATIVE
} ObjectType;

// Table stores properties about a native
// object. This should be statically allocated
// and only one should exist for each type of
// Native value.
typedef struct
{
	const char* name;
	void(*mark)(void*);
	void(*free)(void*);
	void(*toString)(void*);
} NativeProp;

typedef struct sObject
{
	char marked;
	
	ObjectType type;
	struct sObject* next;

	union
	{
		char* string;

		struct
		{
			void* addr;
			const NativeProp* prop;	// Can be used to check type of native (ex. obj->nat.prop == &ArrayProp // this is an Array)
		} nat;
	};
} Object;

typedef enum
{
	VAL_NUM,
	VAL_OBJ
} ValueType;

typedef struct
{
	ValueType type;

	union
	{
		double number;
		Object* obj;
	};
} Value;

typedef Value (*ForeignFunction)(const Value* args, int count);

extern int ProgramCounter;
extern const char* FileName;

void* emalloc(size_t size);
void* erealloc(void* mem, size_t newSize);
char* estrdup(const char* string);

void Mark(Object* obj);

Value NewNative(void* ptr, const NativeProp* prop);
Value NewNumber(double value);
Value NewString(char* string);

void DoPush(Value value);
Value DoPop();

int GetProcId(const char* name);
void CallProc(int id, int nargs);

void BindForeignFunction(ForeignFunction func, const char* name);

void DefineConstNumber(const char* name, double number);
void DefineConstString(const char* name, const char* string);

void ResetCompiler(void);
void CompileFile(FILE* in);

void ResetMachine(void);
void RunMachine(void);

#endif
 

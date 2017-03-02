#ifndef TINY_H
#define TINY_H

#include <stdio.h>

struct sObject;

typedef enum
{
	OBJ_NUM,
	OBJ_STRING,
	OBJ_NATIVE
} ObjectType;

typedef struct sObject
{
	char marked;
	
	ObjectType type;
	struct sObject* next;

	union
	{
		char* string;
		double number;
		
		struct
		{
			void* ptr;
			void(*ptrFree)(void*);
			void(*ptrMark)(void*);
		};
	};
} Object;

extern int ProgramCounter;

void* emalloc(size_t size);
void* erealloc(void* mem, size_t newSize);
char* estrdup(const char* string);

void Mark(Object* obj);
Object* NewNative(void* ptr, void (*ptrFree)(void*), void (*ptrMark)(void*));
Object* NewNumber(double value);
Object* NewString(char* string);
void DoPush(Object* value);
Object* DoPop();
int GetProcId(const char* name);
void CallProc(int id, int nargs);
void BindForeignFunction(void (*fun)(void), char* name);
void InitInterpreter();
void DebugMachineProgram();
void CompileFile(FILE* in);
void RunProgram();
void InterpretFile(FILE* in);
void DeleteInterpreter();

#endif
 

#ifndef TINY_H
#define TINY_H

#include <stdio.h>

struct sObject;

typedef enum
{
	OBJ_NUM,
	OBJ_STRING,
	OBJ_NATIVE,
	OBJ_ARRAY,
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

		struct
		{
			struct sObject** values;
			int length;
			int capacity;
		} array;
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
Object* NewArray(int length);
void DoPush(Object* value);
Object* DoPop();
int GetProcId(const char* name);
void CallProc(int id, int nargs);
void BindForeignFunction(void (*fun)(void), char* name);
void InitInterpreter();
void CompileFile(FILE* in);
void RunProgram();
void InterpretFile(FILE* in);
void DeleteInterpreter();

#endif
 

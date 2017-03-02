#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

static void Len(void)
{
	Object* obj = DoPop();
	if(obj->type == OBJ_STRING)
		DoPush(NewNumber(strlen(obj->string)));
	else
		DoPush(NewNumber(0));
}

typedef struct
{
	Object** values;
	size_t length, capacity;
} Array;

static void ArrayFree(void* ptr)
{
	Array* array = ptr;

	free(array->values);
	free(array);
}

static void ArrayMark(void* ptr)
{
	Array* array = ptr;

	for (size_t i = 0; i < array->length; ++i)
		Mark(array->values[i]);
}

static void CreateArray(void)
{
	Array* array = malloc(sizeof(Array));
	assert(array);

	array->length = 0;
	array->capacity = 0;
	
	DoPush(NewNative(array, ArrayFree, ArrayMark));
}

static void ArrayClear(void)
{
	Object* obj = DoPop();
	Array* array = obj->ptr;

	array->length = 0;
}

static void ArrayPush(void)
{
	Object* value = DoPop();
	Object* obj = DoPop();
	
	Array* array = obj->ptr;

	while(array->length + 1 >= array->capacity)
	{
		if (array->capacity == 0)
			array->capacity = 4;
		else
			array->capacity *= 2;
		array->values = erealloc(array->values, array->capacity * sizeof(Object*));
	}

	array->values[array->length] = value;
	array->length += 1;
}

static void ArrayGet(void)
{
	Object* indexObj = DoPop();
	if (indexObj->type != OBJ_NUM)
	{
		fprintf(stderr, "Attempted to index array with non-numerical value.\n");
		exit(1);
	}

	Object* obj = DoPop();
	Array* array = obj->ptr;

	DoPush(array->values[(int)indexObj->number]);
}

static void ArraySet(void)
{
	Object* value = DoPop();
	Object* indexObj = DoPop();
	if (indexObj->type != OBJ_NUM)
	{
		fprintf(stderr, "Attempted to index array with non-numerical value.\n");
		exit(1);
	}

	Object* obj = DoPop();
	Array* array = obj->ptr;

	array->values[(int)indexObj->number] = value;
}

static void ArrayPop(void)
{
	Object* obj = DoPop();
	Array* array = obj->ptr;

	if(array->length <= 0)
	{
		fprintf(stderr, "Attempted to pop value from empty array\n");
		exit(1);
	}
	
	DoPush(array->values[--array->length]);
}

static void Strcat(void)
{
	char* str2 = DoPop()->string;
	char* str1 = DoPop()->string;
	
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	
	char* newString = emalloc(len1 + len2 + 1);
	strcpy(newString, str1);
	strcpy(newString + len1, str2);
	newString[len1 + len2] = '\0';
	
	Object* str = NewString(newString);
	DoPush(str);
}

static void ToNumber(void)
{
	char* str = DoPop()->string;
	double value = strtod(str, NULL);
	Object* num = NewNumber(value);
	DoPush(num);
}

#define NUMTOSTR_CONV_BUFFER_SIZE	32

static void ToString(void)
{
	double num = DoPop()->number;
	char* string = emalloc(NUMTOSTR_CONV_BUFFER_SIZE);
	sprintf(string, "%.*g", NUMTOSTR_CONV_BUFFER_SIZE, num);
	Object* strObj = NewString(string);
	DoPush(strObj);
}

static void SeedRand(void)
{
	srand(time(NULL));
}

static void Rand(void)
{
	DoPush(NewNumber(rand()));
}

static void Exit(void)
{
	int arg = (int)DoPop()->number;
	
	DeleteInterpreter();
	 
	exit(arg);
}

static void BindStandardLibrary()
{
	BindForeignFunction(Len, "len");

	BindForeignFunction(ArrayPush, "array_push");
	BindForeignFunction(ArrayPop, "array_pop");

	BindForeignFunction(Strcat, "strcat");
	BindForeignFunction(ToNumber, "tonumber");
	BindForeignFunction(ToString, "tostring");
	
	BindForeignFunction(SeedRand, "srand");
	BindForeignFunction(Rand, "rand");

	BindForeignFunction(Exit, "exit");
}

void RunFile(FILE* mainScriptFile, char del)
{
	InitInterpreter();
	BindStandardLibrary();
	CompileFile(mainScriptFile);
	
	if(del)
		fclose(mainScriptFile);

	RunProgram();
	DeleteInterpreter();
}

int main(int argc, char* argv[])
{
	FILE* in = stdin;
	
	if(argc == 2)
	{
		in = fopen(argv[1], "r");
		if(!in)
		{
			fprintf(stderr, "Failed to open file '%s'\n", argv[1]);
			return 1;
		}
	}
	
	RunFile(in, argc == 2);
			
	return 0;
}

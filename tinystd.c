#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

typedef struct
{
	Object** data;
	size_t length;
	size_t capacity;
} Std_Array;

void Std_MarkArray(void* nat)
{
	Std_Array* array = (Std_Array*)nat;
	for(size_t i = 0; i < array->length; ++i)
		Mark(array->data[i]);
}

void Std_FreeArray(void* nat)
{
	Std_Array* array = (Std_Array*)nat;
	if(array->data)
		free(array->data);
	array->length = 0;
	array->capacity = 0;
	free(array);
}

void Std_ExtendArray(Std_Array* array)
{
	if(array->length + 1 >= array->capacity)
	{
		array->capacity *= 2;
		array->data = erealloc(array->data, sizeof(Object*) * array->capacity);
	}
}

void Std_ArrayPush()
{
	Object* value = DoPop();
	assert(value);
	Object* nat = DoPop();
	Std_Array* array = (Std_Array*)(nat->ptr);
	Std_ExtendArray(array);
	
	array->data[array->length++] = value;
	value->marked = 1;
}

void Std_ArrayPop()
{
	Object* nat = DoPop();
	Std_Array* array = (Std_Array*)(nat->ptr);
	if(array->length <= 0)
	{
		fprintf(stderr, "Attempted to 'array_pop' on an array that was empty!\n");
		exit(1);
	}
	
	Object* value = array->data[--array->length];
	DoPush(value);
}

void Std_ArrayGet()
{
	Object* idxObj = DoPop();
	int idx = (int)idxObj->number;
	
	Object* nat = DoPop();
	Std_Array* array = (Std_Array*)(nat->ptr);
	if(array->length <= 0)
	{
		fprintf(stderr, "Attempted to 'array_get' on an array that was empty!\n");
		exit(1);
	}
	
	Object* value = array->data[idx];
	DoPush(value);
}

void Std_ArrayLength()
{
	Object* nat = DoPop();
	Std_Array* array = (Std_Array*)(nat->ptr);
	DoPush(NewNumber(array->length));
}

void Std_CreateArray()
{
	Std_Array* array = emalloc(sizeof(Std_Array));
	array->data = NULL;
	array->length = 0;
	array->capacity = 1;
	Object* nat = NewNative(array, Std_FreeArray, Std_MarkArray);
	DoPush(nat);
}

void Std_Strcat()
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

void Std_ToNumber()
{
	char* str = DoPop()->string;
	double value = strtod(str, NULL);
	Object* num = NewNumber(value);
	DoPush(num);
}

#define STD_NUMTOSTR_CONV_BUFFER_SIZE	32

void Std_ToString()
{
	double num = DoPop()->number;
	char* string = emalloc(STD_NUMTOSTR_CONV_BUFFER_SIZE);
	sprintf(string, "%.*g", num, STD_NUMTOSTR_CONV_BUFFER_SIZE);
	Object* strObj = NewString(string);
	DoPush(strObj);
}

void Std_SeedRand()
{
	srand(time(NULL));
}

void Std_Rand()
{
	DoPush(NewNumber(rand()));
}

void BindStandardLibrary()
{
	BindForeignFunction(Std_CreateArray, "array");
	BindForeignFunction(Std_ArrayPush, "array_push");
	BindForeignFunction(Std_ArrayPop, "array_pop");
	BindForeignFunction(Std_ArrayGet, "array_get");
	BindForeignFunction(Std_ArrayLength, "array_length");

	BindForeignFunction(Std_Strcat, "strcat");
	BindForeignFunction(Std_ToNumber, "tonumber");
	BindForeignFunction(Std_ToString, "tostring");
	
	BindForeignFunction(Std_SeedRand, "srand");
	BindForeignFunction(Std_Rand, "rand");
}

int main(int argc, char* argv[])
{
	FILE* in = stdin;
	
	InitInterpreter();
	BindStandardLibrary();
	
	if(argc == 2)
	{
		in = fopen(argv[1], "r");
		if(!in)
		{
			fprintf(stderr, "Failed to open file '%s'\n", argv[1]);
			return 1;
		}
	}
	
	InterpretFile(in);
	
	if(argc == 2)
		fclose(in);
		
	DeleteInterpreter();
	return 0;
}

#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

void Std_Len()
{
	Object* obj = DoPop();
	if(obj->type == OBJ_ARRAY)
		DoPush(NewNumber((int)obj->array.length));
	else if(obj->type == OBJ_STRING)
		DoPush(NewNumber(strlen(obj->string)));
	else
		DoPush(NewNumber(0));
}

void Std_Push()
{
	Object* value = DoPop();
	Object* obj = DoPop();
	
	obj->array.length += 1;
	while(obj->array.length >= obj->array.capacity)
	{
		obj->array.capacity *= 2;
		obj->array.values = erealloc(obj->array.values, obj->array.capacity * sizeof(Object*));
	}
	obj->array.values[obj->array.length - 1] = value;
}

void Std_Pop()
{
	Object* obj = DoPop();
	if(obj->array.length <= 0)
	{
		fprintf(stderr, "Attempted to pop value from empty array\n");
		exit(1);
	}
	
	DoPush(obj->array.values[--obj->array.length]);
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

void Std_Exit()
{
	
	int arg = (int)DoPop()->number;
	
	DeleteInterpreter();
	 
	exit(arg);
}

void BindStandardLibrary()
{
	BindForeignFunction(Std_Len, "len");

	BindForeignFunction(Std_Push, "push");
	BindForeignFunction(Std_Pop, "pop");

	BindForeignFunction(Std_Strcat, "strcat");
	BindForeignFunction(Std_ToNumber, "tonumber");
	BindForeignFunction(Std_ToString, "tostring");
	
	BindForeignFunction(Std_SeedRand, "srand");
	BindForeignFunction(Std_Rand, "rand");

	BindForeignFunction(Std_Exit, "exit");
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

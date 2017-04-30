#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "dict.h"
#include "iniparser.h"

#ifdef _WIN32
typedef int BOOL;
typedef unsigned long DWORD;
typedef long LONG;
#if !defined(_M_IX86)
typedef __int64 LONGLONG;
#else
typedef double LONGLONG;
#endif

typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG  HighPart;
	};
	struct {
		DWORD LowPart;
		LONG  HighPart;
	} u;
	LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

BOOL __stdcall QueryPerformanceCounter(
	_Out_ LARGE_INTEGER *lpPerformanceCount
);

BOOL __stdcall QueryPerformanceFrequency(
	_Out_ LARGE_INTEGER *lpFrequency
);

void __stdcall Sleep(
	_In_ DWORD dwMilliseconds
);

#endif

static int Strlen(const Value* args, int count)
{
	Value val = args[0];

	if (val.type == VAL_OBJ && val.obj->type == OBJ_STRING)
		DoPush(NewNumber(strlen(val.obj->string)));
	else
		DoPush(NewNumber(-1));

	return 1;
}

static const NativeProp FileProp = {
	"file",
	NULL,
	NULL,
	NULL
};

static int Lib_Fopen(const Value* args, int count)
{
	const char* filename = args[0].obj->string;
	const char* mode = args[1].obj->string;

	FILE* file = fopen(filename, mode);

	if (!file)
		DoPush(NewNumber(0));
	else
		DoPush(NewNative(file, &FileProp));

	return 1;
}

static int Lib_Fsize(const Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	DoPush(NewNumber((double)size));
	return 1;
}

static int Lib_Fread(const Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	int num = (int)args[1].number;

	char* str = emalloc(num + 1);
	
	fread(str, 1, num, file);
	str[num] = '\0';

	DoPush(NewString(str));
	return 1;
}

static int Lib_Fseek(const Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	int pos = (int)args[1].number;

	fseek(file, pos, SEEK_SET);
	return 0;
}

static int Lib_Fwrite(const Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	const char* str = args[1].obj->string;
	int num = count == 3 ? (int)args[2].number : strlen(str);

	DoPush(NewNumber(fwrite(str, 1, num, file)));
	return 1;
}

static int Lib_Fclose(const Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;

	fclose(file);
	return 0;
}

typedef struct sArray
{
	Value* values;
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
	{
		if(array->values[i].type == VAL_OBJ)
			Mark(array->values[i].obj);
	}
}

const NativeProp ArrayProp = {
	"array",
	ArrayMark,
	ArrayFree,
	NULL	// TODO: Implement ArrayString
};

static int CreateArray(const Value* args, int count)
{
	Array* array = emalloc(sizeof(Array));

	array->length = 0;
	array->capacity = 0;
	array->values = NULL;

	if (count >= 1)
	{
		int len = count;
		
		array->length = len;
		array->capacity = len;
		array->values = emalloc(sizeof(Value) * len);

		for (int i = 0; i < count; ++i)
			array->values[i] = args[i];
	}
	
	DoPush(NewNative(array, &ArrayProp));
	return 1;
}

static int ArrayLen(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	DoPush(NewNumber((double)array->length));
	return 1;
}

static int ArrayClear(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	array->length = 0;
	return 0;
}

static int ArrayResize(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	int len = (int)args[1].number;

	array->length = len;

	while (array->length >= array->capacity)
	{
		if (array->capacity == 0)
			array->capacity = 4;
		else
			array->capacity *= 2;
		array->values = erealloc(array->values, array->capacity * sizeof(Value));
	}

	return 0;
}

static int ArrayPush(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	Value value = args[1];

	while(array->length + 1 >= array->capacity)
	{
		if (array->capacity == 0)
			array->capacity = 4;
		else
			array->capacity *= 2;
		array->values = erealloc(array->values, array->capacity * sizeof(Value));
	}

	array->values[array->length] = value;
	array->length += 1;

	return 0;
}

static int ArrayGet(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	int index = (int)args[1].number;

	DoPush(array->values[index]);
	return 1;
}

static int ArraySet(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	int index = (int)args[1].number;
	Value value = args[2];

	if (index < 0 || index >= array->length)
	{
		fprintf(stderr, "Array index out of bounds.\n");
		exit(1);
	}

	array->values[index] = value;
	return 0;
}

static int ArrayPop(const Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	if (array->length <= 0)
	{
		fprintf(stderr, "Attempted to pop value from empty array\n");
		exit(1);
	}

	DoPush(array->values[--array->length]);
	return 1;
}

static int Lib_IniParse(const Value* args, int count)
{
	const char* filename = args[0].obj->string;

	FILE* file = fopen(filename, "rb");

	if (!file)
	{
		fprintf(stderr, "Failed to open file '%s' for reading.\n", filename);
		DoPush(NewNumber(0));
		return 1;
	}
	
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	char* str = emalloc(size + 1);
	fread(str, 1, size, file);

	str[size] = '\0';

	fclose(file);

	IniFile* ini = emalloc(sizeof(IniFile));

	if (ParseIni(ini, str))
		DoPush(NewNative(ini, &IniFileProp));
	else
	{
		free(file);
		DoPush(NewNumber(0));
	}

	free(str);

	return 1;
}

static int Lib_IniSection(const Value* args, int count)
{
	const IniFile* ini = args[0].obj->nat.addr;
	const char* name = args[1].obj->string;

	for (int i = 0; i < ini->count; ++i)
	{
		if (strcmp(ini->sections[i].name, name) == 0)
		{
			DoPush(NewNative(&ini->sections[i], &IniSectionProp));
			return 1;
		}
	}

	DoPush(NewNumber(0));
	return 1;
}

static int Lib_IniValue(const Value* args, int count)
{
	const IniSection* ini = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;

	for (int i = 0; i < ini->count; ++i)
	{
		if (strcmp(ini->keys[i], key) == 0)
		{
			DoPush(NewString(estrdup(ini->values[i])));
			return 1;
		}
	}

	DoPush(NewNumber(0));
	return 1;
}

static int Lib_IniSections(const Value* args, int count)
{
	const IniFile* ini = args[0].obj->nat.addr;

	Array* array = emalloc(sizeof(Array));

	array->capacity = ini->count;
	array->length = ini->count;

	if (array->length > 0)
		array->values = emalloc(sizeof(Value) * array->length);
	else
		array->values = NULL;

	for (int i = 0; i < ini->count; ++i)
		array->values[i] = NewNative(&ini->sections[i], &IniSectionProp);
	
	DoPush(NewNative(array, &ArrayProp));
	return 1;
}

static int Lib_IniName(const Value* args, int count)
{
	const IniSection* ini = args[0].obj->nat.addr;
	
	DoPush(NewString(estrdup(ini->name)));
	return 1;
}

static int Lib_IniKeys(const Value* args, int count)
{
	const IniSection* ini = args[0].obj->nat.addr;

	Array* array = emalloc(sizeof(Array));

	array->capacity = ini->count;
	array->length = ini->count;

	if (array->length > 0)
		array->values = emalloc(sizeof(Value) * array->length);
	else
		array->values = NULL;

	for (int i = 0; i < ini->count; ++i)
		array->values[i] = NewString(estrdup(ini->keys[i]));

	DoPush(NewNative(array, &ArrayProp));
	return 1;
}

typedef struct
{
	int length;
	unsigned char* data;
} Bytes;

static void BytesFree(void* ptr)
{
	Bytes* b = ptr;

	free(b->data);
	free(b);
}

static const NativeProp BytesProp = {
	"bytes",
	NULL,
	BytesFree,
	NULL
};

static int Lib_CreateBytes(const Value* args, int count)
{
	Value val = args[0];

	if (val.type != VAL_OBJ)
	{
		fprintf(stderr, "bytes expected a string or array as its first parameter but got a number instead.\n");
		exit(1);
	}

	Object* obj = val.obj;

	Bytes* bytes = emalloc(sizeof(Bytes));

	if (obj->type == OBJ_STRING)
	{
		bytes->length = strlen(obj->string);
		bytes->data = emalloc(bytes->length);

		memcpy(bytes->data, obj->string, bytes->length);

	}
	else if (obj->type == OBJ_NATIVE)
	{
		if (obj->nat.prop == &ArrayProp)
		{
			Array* array = obj->nat.addr;

			if (array->length == 0)
				DoPush(NewNative(NULL, &BytesProp));
			else
			{
				unsigned char* data = emalloc(array->length);

				for (int i = 0; i < array->length; ++i)
					data[i] = (unsigned char)array->values[i].number;

				bytes->length = array->length;
				bytes->data = data;
			}
		}
	}

	DoPush(NewNative(bytes, &BytesProp));
	return 1;
}

static int Lib_BytesGet(const Value* args, int count)
{
	Bytes* bytes = args[0].obj->nat.addr;
	int index = (int)args[1].number;

	DoPush(NewNumber(bytes->data[index]));
	return 1;
}

static int Lib_BytesLen(const Value* args, int count)
{
	Bytes* bytes = args[0].obj->nat.addr;

	DoPush(NewNumber(bytes->length));
	return 1;
}

static int CreateDict(const Value* args, int count)
{
	Dict* dict = emalloc(sizeof(Dict));

	InitDict(dict);

	if (count > 0 && count % 2 != 0)
	{
		fprintf(stderr, "Expected even number of arguments to dict(...) (since each key needs a corresponding value) but got %d.\n", count);
		exit(1);
	}
	
	for (int i = 0; i < count; i += 2)
		DictPut(dict, args[i].obj->string, &args[i + 1]);

	DoPush(NewNative(dict, &DictProp));
	return 1;
}

static int Lib_DictPut(const Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;
	Value value = args[2];

	DictPut(dict, key, &value);
	return 0;
}

static int Lib_DictExists(const Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;

	const Value* value = DictGet(dict, key);

	if (value)
		DoPush(NewNumber(1));
	else
		DoPush(NewNumber(0));

	return 1;
}

static int Lib_DictGet(const Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;

	const Value* value = DictGet(dict, key);

	if (value)
		DoPush(*value);
	else
		DoPush(NewNumber(0));

	return 1;
}

static int Lib_DictRemove(const Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;
	
	DictRemove(dict, key);
	return 0;
}

static int Lib_DictClear(const Value* args, int count)
{
	DictClear(args[0].obj->nat.addr);
	return 0;
}

static int Lib_DictKeys(const Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	Array* array = emalloc(sizeof(Array));

	array->capacity = dict->nodeCount;
	array->length = 0;
	array->values = emalloc(sizeof(Value) * dict->nodeCount);

	for (int i = 0; i < DICT_BUCKET_COUNT; ++i)
	{
		DictNode* node = dict->buckets[i];

		while (node)
		{
			array->values[array->length++] = NewString(estrdup(node->key));
			node = node->next;
		}
	}

	DoPush(NewNative(array, &ArrayProp));
	return 1;
}

static int Strcat(const Value* args, int count)
{
	char* str1 = args[0].obj->string;
	char* str2 = args[1].obj->string;
	
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	
	char* newString = emalloc(len1 + len2 + 1);
	strcpy(newString, str1);
	strcpy(newString + len1, str2);
	newString[len1 + len2] = '\0';
	
	DoPush(NewString(newString));
	return 1;
}

static int Lib_Ston(const Value* args, int count)
{
	char* str = args[0].obj->string;
	double value = strtod(str, NULL);
	
	DoPush(NewNumber(value));
	return 1;
}

#define NUMTOSTR_CONV_BUFFER_SIZE	32

static int Lib_Ntos(const Value* args, int count)
{
	double num = args[0].number;
	
	char* string = emalloc(NUMTOSTR_CONV_BUFFER_SIZE + 1);
	int c = sprintf(string, "%g", num);

	string[c] = '\0';

	DoPush(NewString(string));
	return 1;
}

static int Lib_Time(const Value* args, int count)
{
	DoPush(NewNumber((double)time(NULL)));
	return 1;
}

static int SeedRand(const Value* args, int count)
{
	srand((unsigned int)args[0].number);
	return 0;
}

static int Rand(const Value* args, int count)
{
	DoPush(NewNumber(rand()));
	return 1;
}

static int Lib_Input(const Value* args, int count)
{
	if (count >= 1)
		printf("%s", args[0].obj->string);

	char* buffer = emalloc(1);
	size_t bufferLength = 1;
	size_t bufferCapacity = 1;

	int c = getc(stdin);
	int i = 0;

	while (c != '\n')
	{
		if (bufferLength + 1 >= bufferCapacity)
		{
			bufferCapacity *= 2;
			buffer = erealloc(buffer, bufferCapacity);
		}

		++bufferLength;
		buffer[i++] = c;
		c = getc(stdin);
	}

	buffer[i] = '\0';

	DoPush(NewString(buffer));
	return 1;
}

static int Lib_Print(const Value* args, int count)
{
	for (int i = 0; i < count; ++i)
	{
		Value val = args[i];

		if (val.type == VAL_NUM)
			printf("%g", val.number);
		else if (val.type == VAL_OBJ)
		{
			if (val.obj->type == OBJ_STRING)
				printf("%s", val.obj->string);
			else if (val.obj->type == OBJ_NATIVE)
			{
				if (val.obj->nat.prop && val.obj->nat.prop->name)
					printf("<native '%s' at %p>", val.obj->nat.prop->name, val.obj->nat.addr);
				else
					printf("<native at %p>", val.obj->nat.addr);
			}
		}

		if (i + 1 < count)
			putc(' ', stdout);
	}

	putc('\n', stdout);
	return 0;
}

static int Lib_Printf(const Value* args, int count)
{
	const char* fmt = args[0].obj->string;

	int arg = 1;

	while (*fmt)
	{
		if (*fmt == '%')
		{
			if (arg >= count)
			{
				fprintf(stderr, "Too few arguments for format '%s'\n", fmt);
				exit(1);
			}

			++fmt;
			switch (*fmt)
			{
				case 'd': printf("%d", (int)args[arg].number); break;
				case 'g': printf("%g", args[arg].number); break;
				case 's': printf("%s", args[arg].obj->string); break;

				default:
					printf("\nInvalid format specifier '%c'\n", *fmt);

				case 'q':
				{
					switch (args[arg].type)
					{
						case VAL_NUM: printf("%g", args[arg].number); break;
						case VAL_OBJ:
						{
							switch (args[arg].obj->type)
							{
								case OBJ_STRING: printf("%s", args[arg].obj->string);
								case OBJ_NATIVE:
								{
									if (args[arg].obj->nat.prop && args[arg].obj->nat.prop->name)
										printf("<native '%s' at %p>", args[arg].obj->nat.prop->name, args[arg].obj->nat.addr);
									else
										printf("<native at %p>", args[arg].obj->nat.addr);
								} break;
							} break;
						} break;
					}
				} break;
			}
			++fmt;
			++arg;
		}
		else
			putc(*fmt++, stdout);
	}

	return 0;
}

static int Exit(const Value* args, int count)
{
	int arg = (int)args[0].number;

	exit(arg);
}

static int Lib_Floor(const Value* args, int count)
{
	DoPush(NewNumber(floor(args[0].number)));
	return 1;
}

static int Lib_Ceil(const Value* args, int count)
{
	DoPush(NewNumber(ceil(args[0].number)));
	return 1;
}

static int Lib_PerfCount(const Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);

	DoPush(NewNumber(result.QuadPart));
	return 1;
}

static int Lib_PerfFreq(const Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceFrequency(&result);

	DoPush(NewNumber(result.QuadPart));
	return 1;
}

static int Lib_Sleep(const Value* args, int count)
{
	Sleep((int)args[0].number);
	return 0;
}

static void BindStandardLibrary()
{
	BindForeignFunction(Strlen, "strlen");

	BindForeignFunction(Lib_Fopen, "fopen");
	BindForeignFunction(Lib_Fclose, "fclose");
	BindForeignFunction(Lib_Fread, "fread");
	BindForeignFunction(Lib_Fwrite, "fwrite");
	BindForeignFunction(Lib_Fseek, "fseek");
	BindForeignFunction(Lib_Fsize, "fsize");

	BindForeignFunction(CreateArray, "array");
	BindForeignFunction(ArrayClear, "array_clear");
	BindForeignFunction(ArrayResize, "array_resize");
	BindForeignFunction(ArrayGet, "array_get");
	BindForeignFunction(ArraySet, "array_set");
	BindForeignFunction(ArrayLen, "array_len");
	BindForeignFunction(ArrayPush, "array_push");
	BindForeignFunction(ArrayPop, "array_pop");

	BindForeignFunction(Lib_CreateBytes, "bytes");
	BindForeignFunction(Lib_BytesGet, "bytes_get");
	BindForeignFunction(Lib_BytesLen, "bytes_len");

	BindForeignFunction(CreateDict, "dict");
	BindForeignFunction(Lib_DictPut, "dict_put");
	BindForeignFunction(Lib_DictExists, "dict_exists");
	BindForeignFunction(Lib_DictGet, "dict_get");
	BindForeignFunction(Lib_DictRemove, "dict_remove");
	BindForeignFunction(Lib_DictKeys, "dict_keys");
	BindForeignFunction(Lib_DictClear, "dict_clear");

	BindForeignFunction(Lib_IniParse, "ini_parse");
	BindForeignFunction(Lib_IniSection, "ini_section");
	BindForeignFunction(Lib_IniValue, "ini_value");
	BindForeignFunction(Lib_IniSections, "ini_sections");
	BindForeignFunction(Lib_IniName, "ini_name");
	BindForeignFunction(Lib_IniKeys, "ini_keys");

	BindForeignFunction(Strcat, "strcat");
	BindForeignFunction(Lib_Ston, "ston");
	BindForeignFunction(Lib_Ntos, "ntos");
	
	BindForeignFunction(Lib_Time, "time");
	BindForeignFunction(SeedRand, "srand");
	BindForeignFunction(Rand, "rand");

	BindForeignFunction(Lib_Floor, "floor");
	BindForeignFunction(Lib_Ceil, "ceil");

	BindForeignFunction(Lib_PerfCount, "perfcount");
	BindForeignFunction(Lib_PerfFreq, "perffreq");
	BindForeignFunction(Lib_Sleep, "sleep");

	BindForeignFunction(Lib_Input, "input");
	BindForeignFunction(Lib_Print, "print");
	BindForeignFunction(Lib_Printf, "printf");

	BindForeignFunction(Exit, "exit");
}

void RunFile(const char* name, FILE* mainScriptFile)
{
	ResetCompiler();
	FileName = name;

	BindStandardLibrary();
	CompileFile(mainScriptFile);
	
	ResetMachine();
	RunMachine();

	ResetCompiler();
	ResetMachine();
}

int main(int argc, char* argv[])
{
	FILE* in = stdin;
	
	if (argc == 2)
	{
		in = fopen(argv[1], "r");
		if (!in)
		{
			fprintf(stderr, "Failed to open file '%s'\n", argv[1]);
			return 1;
		}

		RunFile(argv[1], in);

		fclose(in);
	}
	else
		RunFile(NULL, stdin);

	return 0;
}

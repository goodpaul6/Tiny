#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "dict.h"
#include "iniparser.h"

#define VAL_NULL (Tiny_Null);

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

static Tiny_Value Strlen(const Tiny_Value* args, int count)
{
	Tiny_Value val = args[0];

	if (val.type == TINY_VAL_STRING)
		return Tiny_NewNumber(strlen(val.obj->string));
	else
		return VAL_NULL;
}

static const Tiny_NativeProp FileProp = {
	"file",
	NULL,
	NULL,
	NULL
};

static Tiny_Value Lib_Fopen(const Tiny_Value* args, int count)
{
	const char* filename = args[0].obj->string;
	const char* mode = args[1].obj->string;

	FILE* file = fopen(filename, mode);

	if (!file)
		return VAL_NULL;

	return Tiny_NewNative(file, &FileProp);
}

static Tiny_Value Lib_Fsize(const Tiny_Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	return Tiny_NewNumber((double)size);
}

static Tiny_Value Lib_Fread(const Tiny_Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	int num = (int)args[1].number;

	char* str = emalloc(num + 1);
	
	fread(str, 1, num, file);
	str[num] = '\0';

	return Tiny_NewString(str);
}

static Tiny_Value Lib_Fseek(const Tiny_Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	int pos = (int)args[1].number;

	fseek(file, pos, SEEK_SET);

	return VAL_NULL;
}

static Tiny_Value Lib_Fwrite(const Tiny_Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;
	const char* str = args[1].obj->string;
	int num = count == 3 ? (int)args[2].number : strlen(str);

	return Tiny_NewNumber(fwrite(str, 1, num, file));
}

static Tiny_Value Lib_Fclose(const Tiny_Value* args, int count)
{
	FILE* file = args[0].obj->nat.addr;

	fclose(file);

	return VAL_NULL;
}

typedef struct sArray
{
	Tiny_Value* values;
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
		if(Tiny_IsObject(array->values[i]))
			Tiny_Mark(array->values[i].obj);
	}
}

const Tiny_NativeProp ArrayProp = {
	"array",
	ArrayMark,
	ArrayFree,
	NULL	// TODO: Implement ArrayString
};

static Tiny_Value CreateArray(const Tiny_Value* args, int count)
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
		array->values = emalloc(sizeof(Tiny_Value) * len);

		for (int i = 0; i < count; ++i)
			array->values[i] = args[i];
	}
	
	return Tiny_NewNative(array, &ArrayProp);
}

static Tiny_Value ArrayLen(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	return Tiny_NewNumber((double)array->length);
}

static Tiny_Value ArrayClear(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	array->length = 0;

	return VAL_NULL;
}

static Tiny_Value ArrayResize(const Tiny_Value* args, int count)
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
		array->values = erealloc(array->values, array->capacity * sizeof(Tiny_Value));
	}

	return VAL_NULL;
}

static Tiny_Value ArrayPush(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	Tiny_Value value = args[1];

	while(array->length + 1 >= array->capacity)
	{
		if (array->capacity == 0)
			array->capacity = 4;
		else
			array->capacity *= 2;
		array->values = erealloc(array->values, array->capacity * sizeof(Tiny_Value));
	}

	array->values[array->length] = value;
	array->length += 1;

	return VAL_NULL;
}

static Tiny_Value ArrayGet(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	int index = (int)args[1].number;

	return array->values[index];
}

static Tiny_Value ArraySet(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;
	int index = (int)args[1].number;
	Tiny_Value value = args[2];

	if (index < 0 || index >= array->length)
	{
		fprintf(stderr, "Array index out of bounds.\n");
		exit(1);
	}

	array->values[index] = value;
	return VAL_NULL;
}

static Tiny_Value ArrayPop(const Tiny_Value* args, int count)
{
	Array* array = args[0].obj->nat.addr;

	if (array->length <= 0)
	{
		fprintf(stderr, "Attempted to pop value from empty array\n");
		exit(1);
	}

	return array->values[--array->length];
}

static Tiny_Value Lib_IniNew(const Tiny_Value* args, int count)
{
	IniFile* ini = emalloc(sizeof(IniFile));

	ini->count = 0;
	ini->sections = NULL;

	return Tiny_NewNative(ini, &IniFileProp);
}

static Tiny_Value Lib_IniParse(const Tiny_Value* args, int count)
{
	const char* filename = args[0].obj->string;

	FILE* file = fopen(filename, "rb");

	if (!file)
	{
		fprintf(stderr, "Failed to open file '%s' for reading.\n", filename);
		return VAL_NULL;
	}
	
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	char* str = emalloc(size + 1);
	fread(str, 1, size, file);

	str[size] = '\0';

	fclose(file);

	IniFile* ini = emalloc(sizeof(IniFile));
	Tiny_Value result;

	if (ParseIni(ini, str))
		result = Tiny_NewNative(ini, &IniFileProp);
	else
	{
		free(file);
		result = VAL_NULL;
	}

	free(str);

	return result;
}

static Tiny_Value Lib_IniGet(const Tiny_Value* args, int count)
{
	const IniFile* ini = args[0].obj->nat.addr;
	const char* section = args[1].obj->string;
	const char* key = args[2].obj->string;

	for (int i = 0; i < ini->count; ++i)
	{
		if (strcmp(ini->sections[i].name, section) == 0)
		{
			const IniSection* sec = &ini->sections[i];

			for (int i = 0; i < sec->count; ++i)
			{
				if (strcmp(sec->keys[i], key) == 0)
					return Tiny_NewString(estrdup(sec->values[i]));
			}

			return Tiny_NewNumber(INI_NO_KEY);
		}
	}

	return Tiny_NewNumber(INI_NO_SECTION);
}

static Tiny_Value Lib_IniSet(const Tiny_Value* args, int count)
{
	IniFile* ini = args[0].obj->nat.addr;
	const char* section = args[1].obj->string;
	const char* key = args[2].obj->string;
	const char* value = args[3].obj->string;

	return Tiny_NewNumber(IniSet(ini, section, key, value));
}

static Tiny_Value Lib_IniDelete(const Tiny_Value* args, int count)
{
	IniFile* ini = args[0].obj->nat.addr;
	const char* section = args[1].obj->string;
	const char* key = args[2].obj->string;
	bool removeSection = args[3].number > 0;

	if (strlen(key) == 0)
		key = NULL;

	return Tiny_NewNumber(IniDelete(ini, section, key, removeSection));
}

static Tiny_Value Lib_IniSections(const Tiny_Value* args, int count)
{
	const IniFile* ini = args[0].obj->nat.addr;

	Array* array = emalloc(sizeof(Array));

	array->capacity = ini->count;
	array->length = ini->count;

	if (array->length > 0)
		array->values = emalloc(sizeof(Tiny_Value) * array->length);
	else
		array->values = NULL;

	for (int i = 0; i < ini->count; ++i)
		array->values[i] = Tiny_NewNative(&ini->sections[i], &IniSectionProp);
	
	return Tiny_NewNative(array, &ArrayProp);
}

static Tiny_Value Lib_IniName(const Tiny_Value* args, int count)
{
	const IniSection* ini = args[0].obj->nat.addr;
	
	return Tiny_NewString(estrdup(ini->name));
}

static Tiny_Value Lib_IniKeys(const Tiny_Value* args, int count)
{
	const IniSection* ini = args[0].obj->nat.addr;

	Array* array = emalloc(sizeof(Array));

	array->capacity = ini->count;
	array->length = ini->count;

	if (array->length > 0)
		array->values = emalloc(sizeof(Tiny_Value) * array->length);
	else
		array->values = NULL;

	for (int i = 0; i < ini->count; ++i)
		array->values[i] = Tiny_NewString(estrdup(ini->keys[i]));

	return Tiny_NewNative(array, &ArrayProp);
}

static Tiny_Value Lib_IniString(const Tiny_Value* args, int count)
{
	const IniFile* ini = args[0].obj->nat.addr;
	return Tiny_NewString(IniString(ini));
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

static const Tiny_NativeProp BytesProp = {
	"bytes",
	NULL,
	BytesFree,
	NULL
};

static Tiny_Value Lib_CreateBytes(const Tiny_Value* args, int count)
{
	Tiny_Value val = args[0];

	if (val.type != TINY_VAL_STRING && val.type != TINY_VAL_NATIVE)
	{
		fprintf(stderr, "bytes expected a string or array as its first parameter but got a number instead.\n");
		exit(1);
	}

	Tiny_Object* obj = val.obj;

	Bytes* bytes = emalloc(sizeof(Bytes));

	if (obj->type == TINY_VAL_STRING)
	{
		bytes->length = strlen(obj->string);
		bytes->data = emalloc(bytes->length);

		memcpy(bytes->data, obj->string, bytes->length);

	}
	else if (obj->type == TINY_VAL_NATIVE)
	{
		if (obj->nat.prop == &ArrayProp)
		{
			Array* array = obj->nat.addr;

			if (array->length == 0)
				return Tiny_NewNative(NULL, &BytesProp);
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

	return Tiny_NewNative(bytes, &BytesProp);
}

static Tiny_Value Lib_BytesGet(const Tiny_Value* args, int count)
{
	Bytes* bytes = args[0].obj->nat.addr;
	int index = (int)args[1].number;

	return Tiny_NewNumber(bytes->data[index]);
}

static Tiny_Value Lib_BytesLen(const Tiny_Value* args, int count)
{
	Bytes* bytes = args[0].obj->nat.addr;

	return Tiny_NewNumber(bytes->length);
}

static Tiny_Value CreateDict(const Tiny_Value* args, int count)
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

	return Tiny_NewNative(dict, &DictProp);
}

static Tiny_Value Lib_DictPut(const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;
	Tiny_Value value = args[2];

	DictPut(dict, key, &value);
	return VAL_NULL;
}

static Tiny_Value Lib_DictExists(const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;

	const Tiny_Value* value = DictGet(dict, key);

	return Tiny_NewNumber(value ? 1 : -1);
}

static Tiny_Value Lib_DictGet(const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;

	const Tiny_Value* value = DictGet(dict, key);

	if (value)
		return *value;

	return VAL_NULL;
}

static Tiny_Value Lib_DictRemove(const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = args[1].obj->string;
	
	DictRemove(dict, key);
	return VAL_NULL;
}

static Tiny_Value Lib_DictClear(const Tiny_Value* args, int count)
{
	DictClear(args[0].obj->nat.addr);
	return VAL_NULL;
}

static Tiny_Value Lib_DictKeys(const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	Array* array = emalloc(sizeof(Array));

	array->capacity = dict->nodeCount;
	array->length = 0;
	array->values = emalloc(sizeof(Tiny_Value) * dict->nodeCount);

	for (int i = 0; i < DICT_BUCKET_COUNT; ++i)
	{
		DictNode* node = dict->buckets[i];

		while (node)
		{
			array->values[array->length++] = Tiny_NewString(estrdup(node->key));
			node = node->next;
		}
	}

	return Tiny_NewNative(array, &ArrayProp);
}

static Tiny_Value Strcat(const Tiny_Value* args, int count)
{
	char* str1 = args[0].obj->string;
	char* str2 = args[1].obj->string;
	
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	
	char* newString = emalloc(len1 + len2 + 1);
	strcpy(newString, str1);
	strcpy(newString + len1, str2);
	newString[len1 + len2] = '\0';
	
	return Tiny_NewString(newString);
}

static Tiny_Value Lib_Ston(const Tiny_Value* args, int count)
{
	char* str = args[0].obj->string;
	double value = strtod(str, NULL);
	
	return Tiny_NewNumber(value);
}

#define NUMTOSTR_CONV_BUFFER_SIZE	32

static Tiny_Value Lib_Ntos(const Tiny_Value* args, int count)
{
	double num = args[0].number;
	
	char* string = emalloc(NUMTOSTR_CONV_BUFFER_SIZE + 1);
	int c = sprintf(string, "%g", num);

	string[c] = '\0';

	return Tiny_NewString(string);
}

static Tiny_Value Lib_Time(const Tiny_Value* args, int count)
{
	return Tiny_NewNumber((double)time(NULL));
}

static Tiny_Value SeedRand(const Tiny_Value* args, int count)
{
	srand((unsigned int)args[0].number);
	return VAL_NULL;
}

static Tiny_Value Rand(const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(rand());
}

static Tiny_Value Lib_Input(const Tiny_Value* args, int count)
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

	return Tiny_NewString(buffer);
}

static Tiny_Value Lib_Print(const Tiny_Value* args, int count)
{
	for (int i = 0; i < count; ++i)
	{
		Tiny_Value val = args[i];

		if (val.type == TINY_VAL_NUM)
			printf("%g", val.number);
		else if (val.type == TINY_VAL_STRING)
			printf("%s", val.obj->string);
		else if (val.type == TINY_VAL_NATIVE)
		{
			if (val.obj->nat.prop && val.obj->nat.prop->name)
				printf("<native '%s' at %p>", val.obj->nat.prop->name, val.obj->nat.addr);
			else
				printf("<native at %p>", val.obj->nat.addr);
		}

		if (i + 1 < count)
			putc(' ', stdout);
	}

	putc('\n', stdout);
	return VAL_NULL;
}

static Tiny_Value Lib_Printf(const Tiny_Value* args, int count)
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
						case TINY_VAL_NUM: printf("%g", args[arg].number); break;
						case TINY_VAL_STRING: printf("%s", args[arg].obj->string);
						case TINY_VAL_NATIVE:
						{
							if (args[arg].obj->nat.prop && args[arg].obj->nat.prop->name)
								printf("<native '%s' at %p>", args[arg].obj->nat.prop->name, args[arg].obj->nat.addr);
							else
								printf("<native at %p>", args[arg].obj->nat.addr);
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

	return VAL_NULL;
}

static Tiny_Value Exit(const Tiny_Value* args, int count)
{
	int arg = (int)args[0].number;

	exit(arg);
	
	return VAL_NULL;
}

static Tiny_Value Lib_Floor(const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(floor(args[0].number));
}

static Tiny_Value Lib_Ceil(const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(ceil(args[0].number));
}

static Tiny_Value Lib_PerfCount(const Tiny_Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);

	return Tiny_NewNumber(result.QuadPart);
}

static Tiny_Value Lib_PerfFreq(const Tiny_Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceFrequency(&result);

	return Tiny_NewNumber(result.QuadPart);
}

static Tiny_Value Lib_Sleep(const Tiny_Value* args, int count)
{
	Sleep((int)args[0].number);
	return VAL_NULL;
}

static void BindStandardLibrary()
{
	Tiny_BindForeignFunction(Strlen, "strlen");

	Tiny_BindForeignFunction(Lib_Fopen, "fopen");
	Tiny_BindForeignFunction(Lib_Fclose, "fclose");
	Tiny_BindForeignFunction(Lib_Fread, "fread");
	Tiny_BindForeignFunction(Lib_Fwrite, "fwrite");
	Tiny_BindForeignFunction(Lib_Fseek, "fseek");
	Tiny_BindForeignFunction(Lib_Fsize, "fsize");

	Tiny_BindForeignFunction(CreateArray, "array");
	Tiny_BindForeignFunction(ArrayClear, "array_clear");
	Tiny_BindForeignFunction(ArrayResize, "array_resize");
	Tiny_BindForeignFunction(ArrayGet, "array_get");
	Tiny_BindForeignFunction(ArraySet, "array_set");
	Tiny_BindForeignFunction(ArrayLen, "array_len");
	Tiny_BindForeignFunction(ArrayPush, "array_push");
	Tiny_BindForeignFunction(ArrayPop, "array_pop");

	Tiny_BindForeignFunction(Lib_CreateBytes, "bytes");
	Tiny_BindForeignFunction(Lib_BytesGet, "bytes_get");
	Tiny_BindForeignFunction(Lib_BytesLen, "bytes_len");

	Tiny_BindForeignFunction(CreateDict, "dict");
	Tiny_BindForeignFunction(Lib_DictPut, "dict_put");
	Tiny_BindForeignFunction(Lib_DictExists, "dict_exists");
	Tiny_BindForeignFunction(Lib_DictGet, "dict_get");
	Tiny_BindForeignFunction(Lib_DictRemove, "dict_remove");
	Tiny_BindForeignFunction(Lib_DictKeys, "dict_keys");
	Tiny_BindForeignFunction(Lib_DictClear, "dict_clear");

	Tiny_DefineConstNumber("INI_SUCCESS", INI_SUCCESS);
	Tiny_DefineConstNumber("INI_NEW_KEY", INI_NEW_KEY);
	Tiny_DefineConstNumber("INI_NEW_SECTION", INI_NEW_SECTION);

	Tiny_DefineConstNumber("INI_NO_SECTION", INI_NO_SECTION);
	Tiny_DefineConstNumber("INI_NO_KEY", INI_NO_KEY);
	
	Tiny_BindForeignFunction(Lib_IniNew, "ini_new");
	Tiny_BindForeignFunction(Lib_IniParse, "ini_parse");

	Tiny_BindForeignFunction(Lib_IniGet, "ini_get");
	Tiny_BindForeignFunction(Lib_IniSet, "ini_set");
	Tiny_BindForeignFunction(Lib_IniDelete, "ini_delete");

	Tiny_BindForeignFunction(Lib_IniSections, "ini_sections");
	Tiny_BindForeignFunction(Lib_IniName, "ini_name");
	Tiny_BindForeignFunction(Lib_IniKeys, "ini_keys");

	Tiny_BindForeignFunction(Lib_IniString, "ini_string");

	Tiny_BindForeignFunction(Strcat, "strcat");
	Tiny_BindForeignFunction(Lib_Ston, "ston");
	Tiny_BindForeignFunction(Lib_Ntos, "ntos");
	
	Tiny_BindForeignFunction(Lib_Time, "time");
	Tiny_BindForeignFunction(SeedRand, "srand");
	Tiny_BindForeignFunction(Rand, "rand");

	Tiny_BindForeignFunction(Lib_Floor, "floor");
	Tiny_BindForeignFunction(Lib_Ceil, "ceil");

	Tiny_BindForeignFunction(Lib_PerfCount, "perf_count");
	Tiny_BindForeignFunction(Lib_PerfFreq, "perf_freq");
	Tiny_BindForeignFunction(Lib_Sleep, "sleep");

	Tiny_BindForeignFunction(Lib_Input, "input");
	Tiny_BindForeignFunction(Lib_Print, "print");
	Tiny_BindForeignFunction(Lib_Printf, "printf");

	Tiny_BindForeignFunction(Exit, "exit");
}

void RunFile(const char* name, FILE* mainScriptFile)
{
	Tiny_ResetCompiler();
	
	BindStandardLibrary();
	Tiny_CompileFile(name, mainScriptFile);
	
	Tiny_ResetMachine();
	Tiny_RunMachine();

	Tiny_ResetCompiler();
	Tiny_ResetMachine();
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

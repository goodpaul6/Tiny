#include "tiny.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "dict.h"
#include "tiny_detail.h"

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
	LARGE_INTEGER *lpPerformanceCount
);

BOOL __stdcall QueryPerformanceFrequency(
	LARGE_INTEGER *lpFrequency
);

void __stdcall Sleep(
	DWORD dwMilliseconds
);

#endif

static Tiny_Value Strlen(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Tiny_Value val = args[0];

	if (val.type == TINY_VAL_STRING)
		return Tiny_NewNumber(strlen(Tiny_ToString(val)));
	else
		return Tiny_Null;
}

static Tiny_Value Strchar(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    const char* s = Tiny_ToString(args[0]);
    size_t len = strlen(s);

    int i = (int)Tiny_ToNumber(args[1]);

    assert(i >= 0 && i < len);

    return Tiny_NewNumber((double)s[i]);
}

static const Tiny_NativeProp FileProp = {
	"file",
	NULL,
	NULL,
	NULL
};

static Tiny_Value Lib_Fopen(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	const char* filename = Tiny_ToString(args[0]);
	const char* mode = Tiny_ToString(args[1]);

	FILE* file = fopen(filename, mode);

	if (!file)
		return Tiny_Null;

	return Tiny_NewNative(thread, file, &FileProp);
}

static Tiny_Value Lib_Fsize(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    FILE* file = Tiny_ToAddr(args[0]);

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	return Tiny_NewNumber((double)size);
}

static Tiny_Value Lib_Fread(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	FILE* file = Tiny_ToAddr(args[0]);
	int num = (int)Tiny_ToNumber(args[1]);

	char* str = emalloc(num + 1);
	
	fread(str, 1, num, file);
	str[num] = '\0';

	return Tiny_NewString(thread, str);
}

static Tiny_Value Lib_Fseek(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	FILE* file = Tiny_ToAddr(args[0]);
	int pos = (int)Tiny_ToNumber(args[1]);

	fseek(file, pos, SEEK_SET);

	return Tiny_Null;
}

static Tiny_Value Lib_Fwrite(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	FILE* file = Tiny_ToAddr(args[0]);
	const char* str = Tiny_ToString(args[1]);
	int num = count == 3 ? (int)Tiny_ToNumber(args[2]) : (int)strlen(str);

	return Tiny_NewNumber(fwrite(str, 1, num, file));
}

static Tiny_Value Lib_Fclose(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	FILE* file = Tiny_ToAddr(args[0]);

	fclose(file);

	return Tiny_Null;
}

static void ArrayFree(void* ptr)
{
	Array* array = ptr;

    DestroyArray(array); 
    free(array);
}

static void ArrayMark(void* ptr)
{
	Array* array = ptr;

	for (int i = 0; i < array->length; ++i)
        Tiny_ProtectFromGC(ArrayGetValue(array, i, Tiny_Value));
}

const Tiny_NativeProp ArrayProp = {
	"array",
	ArrayMark,
	ArrayFree,
	NULL	// TODO: Implement ArrayString
};

static Tiny_Value CreateArray(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = emalloc(sizeof(Array));
    
    InitArray(array, sizeof(Tiny_Value));

	if (count >= 1)
	{
        ArrayResize(array, count, NULL);
        
        for(int i = 0; i < array->length; ++i)
            ArraySet(array, i, &args[i]);
	}
	
	return Tiny_NewNative(thread, array, &ArrayProp);
}

static Tiny_Value Lib_ArrayLen(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);

	return Tiny_NewNumber((double)array->length);
}

static Tiny_Value Lib_ArrayClear(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);
    ArrayClear(array);

	return Tiny_Null;
}

static Tiny_Value Lib_ArrayResize(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);
    ArrayResize(array, (int)Tiny_ToNumber(args[0]), NULL);

	return Tiny_Null;
}

static Tiny_Value Lib_ArrayPush(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);
	Tiny_Value value = args[1];
    
    ArrayPush(array, &value);
    
	return Tiny_Null;
}

static Tiny_Value Lib_ArrayGet(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);
	int index = (int)args[1].number;

    return ArrayGetValue(array, index, Tiny_Value);
}

static Tiny_Value Lib_ArraySet(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);
	int index = (int)args[1].number;
	Tiny_Value value = args[2];

    ArraySet(array, index, &value);

    return Tiny_Null;
}

static Tiny_Value Lib_ArrayPop(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Array* array = Tiny_ToAddr(args[0]);

    Tiny_Value value;
    ArrayPop(array, &value);

    return value;
}

static void DictProtectFromGC(void* p)
{
	Dict* d = p;

	for (int i = 0; i < d->bucketCount; ++i) {
		const char* key = ArrayGetValue(&d->keys, i, const char*);
		if (key) {
			Tiny_ProtectFromGC(ArrayGetValue(&d->values, i, Tiny_Value));
		}
	}
}

static void DictFree(void* d)
{
	DestroyDict(d);
	free(d);
}

const Tiny_NativeProp DictProp = {
	"dict",
	DictProtectFromGC,
	DictFree,
	NULL
};

static Tiny_Value CreateDict(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = emalloc(sizeof(Dict));

	InitDict(dict, sizeof(Tiny_Value));

	if (count > 0 && count % 2 != 0)
	{
		fprintf(stderr, "Expected even number of arguments to dict(...) (since each key needs a corresponding value) but got %d.\n", count);
		exit(1);
	}
	
	for (int i = 0; i < count; i += 2)
		DictSet(dict, Tiny_ToString(args[i]), &args[i + 1]);

	return Tiny_NewNative(thread, dict, &DictProp);
}

static Tiny_Value Lib_DictPut(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = Tiny_ToString(args[1]);
	Tiny_Value value = args[2];

	DictSet(dict, key, &value);
	return Tiny_Null;
}

static Tiny_Value Lib_DictExists(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = Tiny_ToString(args[1]);

	const Tiny_Value* value = DictGet(dict, key);

	return Tiny_NewNumber(value ? 1 : -1);
}

static Tiny_Value Lib_DictGet(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = Tiny_ToString(args[1]);

	const Tiny_Value* value = DictGet(dict, key);

	if (value)
		return *value;

	return Tiny_Null;
}

static Tiny_Value Lib_DictRemove(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;
	const char* key = Tiny_ToString(args[1]);
	
	DictRemove(dict, key);
	return Tiny_Null;
}

static Tiny_Value Lib_DictClear(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	DictClear(args[0].obj->nat.addr);
	return Tiny_Null;
}

static Tiny_Value Lib_DictKeys(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Dict* dict = args[0].obj->nat.addr;

	Array* array = emalloc(sizeof(Array));

	array->capacity = dict->filledCount;
	array->length = 0;
	array->data = emalloc(sizeof(Tiny_Value) * dict->filledCount);

	for (int i = 0; i < dict->bucketCount; ++i)
	{
		const char* key = ArrayGetValue(&dict->keys, i, const char*);
		if (key) {
			*((Tiny_Value*)array->data + array->length) = ArrayGetValue(&dict->values, i, Tiny_Value);
			array->length += 1;
		}
	}

	return Tiny_NewNative(thread, array, &ArrayProp);
}

static Tiny_Value Strcat(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	const char* str1 = Tiny_ToString(args[0]);
	const char* str2 = Tiny_ToString(args[1]);
	
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	
	char* newString = emalloc(len1 + len2 + 1);
	strcpy(newString, str1);
	strcpy(newString + len1, str2);
	newString[len1 + len2] = '\0';
	
	return Tiny_NewString(thread, newString);
}

static Tiny_Value Lib_Ston(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	const char* str = Tiny_ToString(args[0]);
	double value = strtod(str, NULL);
	
	return Tiny_NewNumber(value);
}

#define NUMTOSTR_CONV_BUFFER_SIZE	32

static Tiny_Value Lib_Ntos(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	double num = args[0].number;
	
	char* string = emalloc(NUMTOSTR_CONV_BUFFER_SIZE + 1);
	int c = sprintf(string, "%g", num);

	string[c] = '\0';

	return Tiny_NewString(thread, string);
}

static Tiny_Value Lib_Time(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewNumber((double)time(NULL));
}

static Tiny_Value SeedRand(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	srand((unsigned int)args[0].number);
	return Tiny_Null;
}

static Tiny_Value Rand(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(rand());
}

static Tiny_Value Lib_Input(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	if (count >= 1)
		printf("%s", Tiny_ToString(args[0]));

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

	return Tiny_NewString(thread, buffer);
}

static Tiny_Value Lib_Print(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	for (int i = 0; i < count; ++i)
	{
		Tiny_Value val = args[i];

		if (val.type == TINY_VAL_NUM)
			printf("%g", val.number);
		else if (val.type == TINY_VAL_STRING || val.type == TINY_VAL_CONST_STRING)
			printf("%s", Tiny_ToString(val));
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
	return Tiny_Null;
}

static Tiny_Value Lib_Printf(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	const char* fmt = Tiny_ToString(args[0]);

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
				case 's': printf("%s", Tiny_ToString(args[arg])); break;

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

	return Tiny_Null;
}

static Tiny_Value Exit(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	int arg = (int)args[0].number;

	exit(arg);
	
	return Tiny_Null;
}

static Tiny_Value Lib_Floor(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(floor(args[0].number));
}

static Tiny_Value Lib_Ceil(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewNumber(ceil(args[0].number));
}

static Tiny_Value Lib_PerfCount(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);

	return Tiny_NewNumber((double)result.QuadPart);
}

static Tiny_Value Lib_PerfFreq(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	LARGE_INTEGER result;
	QueryPerformanceFrequency(&result);

	return Tiny_NewNumber((double)result.QuadPart);
}

static Tiny_Value Lib_Sleep(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Sleep((int)args[0].number);
	return Tiny_Null;
}

void Tiny_BindStandardArray(Tiny_State* state)
{
	Tiny_BindFunction(state, "array", CreateArray);
	Tiny_BindFunction(state, "array_clear", Lib_ArrayClear);
	Tiny_BindFunction(state, "array_resize", Lib_ArrayResize);
	Tiny_BindFunction(state, "array_get", Lib_ArrayGet);
	Tiny_BindFunction(state, "array_set", Lib_ArraySet);
	Tiny_BindFunction(state, "array_len", Lib_ArrayLen);
	Tiny_BindFunction(state, "array_push", Lib_ArrayPush);
	Tiny_BindFunction(state, "array_pop", Lib_ArrayPop);
}

void Tiny_BindStandardDict(Tiny_State* state)
{
	Tiny_BindFunction(state, "dict", CreateDict);
	Tiny_BindFunction(state, "dict_put", Lib_DictPut);
	Tiny_BindFunction(state, "dict_exists", Lib_DictExists);
	Tiny_BindFunction(state, "dict_get", Lib_DictGet);
	Tiny_BindFunction(state, "dict_remove", Lib_DictRemove);
	Tiny_BindFunction(state, "dict_keys", Lib_DictKeys);
	Tiny_BindFunction(state, "dict_clear", Lib_DictClear);
}

void Tiny_BindStandardIO(Tiny_State* state)
{
	Tiny_BindFunction(state, "fopen", Lib_Fopen);
	Tiny_BindFunction(state, "fclose", Lib_Fclose);
	Tiny_BindFunction(state, "fread", Lib_Fread);
	Tiny_BindFunction(state, "fwrite", Lib_Fwrite);
	Tiny_BindFunction(state, "fseek", Lib_Fseek);
	Tiny_BindFunction(state, "fsize", Lib_Fsize);

	Tiny_BindFunction(state, "input", Lib_Input);
	Tiny_BindFunction(state, "print", Lib_Print);
	Tiny_BindFunction(state, "printf", Lib_Printf);
}

void Tiny_BindStandardLib(Tiny_State* state)
{
	Tiny_BindFunction(state, "strlen", Strlen);
	Tiny_BindFunction(state, "strchar", Strchar);

	Tiny_BindFunction(state, "strcat", Strcat);
	Tiny_BindFunction(state, "ston", Lib_Ston);
	Tiny_BindFunction(state, "ntos", Lib_Ntos);
	
	Tiny_BindFunction(state, "time", Lib_Time);
	Tiny_BindFunction(state, "srand", SeedRand);
	Tiny_BindFunction(state, "rand", Rand);

	Tiny_BindFunction(state, "floor", Lib_Floor);
	Tiny_BindFunction(state, "ceil", Lib_Ceil);

	Tiny_BindFunction(state, "perf_count", Lib_PerfCount);
	Tiny_BindFunction(state, "perf_freq", Lib_PerfFreq);
	Tiny_BindFunction(state, "sleep", Lib_Sleep);

	Tiny_BindFunction(state, "exit", Exit);
}

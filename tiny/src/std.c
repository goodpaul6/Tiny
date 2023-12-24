#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "detail.h"
#include "dict.h"
#include "tiny.h"

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
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

BOOL __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);

BOOL __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

void __stdcall Sleep(DWORD dwMilliseconds);

#endif

static Tiny_Value Strlen(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Tiny_Value val = args[0];

    return Tiny_NewInt(strlen(Tiny_ToString(val)));
}

static Tiny_Value Stridx(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    const char *s = Tiny_ToString(args[0]);
    size_t len = strlen(s);

    int i = Tiny_ToInt(args[1]);

    assert(i >= 0 && i < len);

    return Tiny_NewInt(s[i]);
}

static TINY_FOREIGN_FUNCTION(Strchr) {
    const char *s = Tiny_ToString(args[0]);
    size_t len = Tiny_StringLen(args[0]);
    char c = Tiny_ToInt(args[1]);

    const char *cs = memchr(s, c, len);

    if (cs) {
        return Tiny_NewInt((int)(cs - s));
    }

    return Tiny_NewInt(-1);
}

static const Tiny_NativeProp FileProp = {
    "file",
    NULL,
    NULL,
};

static Tiny_Value Lib_Fopen(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    const char *filename = Tiny_ToString(args[0]);
    const char *mode = Tiny_ToString(args[1]);

    FILE *file = fopen(filename, mode);

    if (!file) return Tiny_Null;

    return Tiny_NewNative(thread, file, &FileProp);
}

static Tiny_Value Lib_Fsize(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = Tiny_ToAddr(args[0]);

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    return Tiny_NewInt((int)size);
}

static Tiny_Value Lib_Fread(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = Tiny_ToAddr(args[0]);
    int num = (int)Tiny_ToNumber(args[1]);

    // + 1 for null-terminator
    char *str = Tiny_AllocUsingContext(thread, NULL, num + 1);

    int readCount = fread(str, 1, num, file);

    if (readCount < 0) {
        Tiny_AllocUsingContext(thread, str, 0);
        return Tiny_NewConstString("");
    }

    str[num] = '\0';

    return Tiny_NewString(thread, str, readCount);
}

static Tiny_Value Lib_Fseek(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = Tiny_ToAddr(args[0]);
    int pos = (int)Tiny_ToNumber(args[1]);

    fseek(file, pos, SEEK_SET);

    return Tiny_Null;
}

static Tiny_Value Lib_Fwrite(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = Tiny_ToAddr(args[0]);
    const char *str = Tiny_ToString(args[1]);
    int num = count == 3 ? (int)Tiny_ToNumber(args[2]) : (int)strlen(str);

    return Tiny_NewInt(fwrite(str, 1, num, file));
}

static Tiny_Value Lib_Fclose(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = Tiny_ToAddr(args[0]);

    fclose(file);

    return Tiny_Null;
}

static Tiny_Value Lib_ReadFile(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = fopen(Tiny_ToString(args[0]), "rb");

    if (!file) {
        return Tiny_Null;
    }

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    char *s = Tiny_AllocUsingContext(thread, NULL, len + 1);
    fread(s, 1, len, file);

    s[len] = '\0';

    fclose(file);

    return Tiny_NewString(thread, s, len);
}

static Tiny_Value Lib_WriteFile(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    FILE *file = fopen(Tiny_ToString(args[0]), "w");

    if (!file) {
        return Tiny_NewBool(false);
    }

    const char *s = Tiny_ToString(args[1]);

    fwrite(s, 1, strlen(s), file);
    fclose(file);

    return Tiny_NewBool(true);
}

static void ArrayFree(void *ptr) {
    Array *array = ptr;

    DestroyArray(array);
    free(array);
}

static void ArrayMark(void *ptr) {
    Array *array = ptr;

    for (int i = 0; i < array->length; ++i) Tiny_ProtectFromGC(ArrayGetValue(array, i, Tiny_Value));
}

const Tiny_NativeProp ArrayProp = {
    "array",
    ArrayMark,
    ArrayFree,
};

static Tiny_Value CreateArray(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = malloc(sizeof(Array));

    InitArray(array, sizeof(Tiny_Value));

    if (count >= 1) {
        ArrayResize(array, count, NULL);

        for (int i = 0; i < array->length; ++i) ArraySet(array, i, &args[i]);
    }

    return Tiny_NewNative(thread, array, &ArrayProp);
}

static Tiny_Value Lib_ArrayLen(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);

    return Tiny_NewInt((double)array->length);
}

static Tiny_Value Lib_ArrayClear(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    ArrayClear(array);

    return Tiny_Null;
}

static Tiny_Value Lib_ArrayResize(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    ArrayResize(array, (int)Tiny_ToNumber(args[0]), NULL);

    return Tiny_Null;
}

static Tiny_Value Lib_ArrayPush(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    Tiny_Value value = args[1];

    ArrayPush(array, &value);

    return Tiny_Null;
}

static Tiny_Value Lib_ArrayGet(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    int index = Tiny_ToInt(args[1]);

    return ArrayGetValue(array, index, Tiny_Value);
}

static Tiny_Value Lib_ArraySet(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    int index = Tiny_ToInt(args[1]);
    Tiny_Value value = args[2];

    ArraySet(array, index, &value);

    return Tiny_Null;
}

static Tiny_Value Lib_ArrayPop(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);

    Tiny_Value value;
    ArrayPop(array, &value);

    return value;
}

static Tiny_Value Lib_ArrayShift(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);

    Tiny_Value value;
    ArrayShift(array, &value);

    return value;
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayRemove) {
    Array *array = Tiny_ToAddr(args[0]);
    int idx = Tiny_ToInt(args[1]);

    ArrayRemove(array, idx);

    return Tiny_Null;
}

static void DictProtectFromGC(void *p) {
    Dict *d = p;

    for (int i = 0; i < d->bucketCount; ++i) {
        const char *key = ArrayGetValue(&d->keys, i, const char *);
        if (key) {
            Tiny_ProtectFromGC(ArrayGetValue(&d->values, i, Tiny_Value));
        }
    }
}

static void DictFree(void *d) {
    DestroyDict(d);
    free(d);
}

const Tiny_NativeProp DictProp = {"dict", DictProtectFromGC, DictFree};

static Tiny_Value CreateDict(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = malloc(sizeof(Dict));

    InitDict(dict, sizeof(Tiny_Value));

    if (count > 0 && count % 2 != 0) {
        fprintf(stderr,
                "Expected even number of arguments to dict(...) (since each key "
                "needs a corresponding value) but got %d.\n",
                count);
        exit(1);
    }

    for (int i = 0; i < count; i += 2) DictSet(dict, Tiny_ToString(args[i]), &args[i + 1]);

    return Tiny_NewNative(thread, dict, &DictProp);
}

static char *MakeNullTerminated(Tiny_StateThread *thread, char *staticBuf, size_t staticBufLen,
                                Tiny_Value str) {
    const char *s = Tiny_ToString(str);
    size_t len = Tiny_StringLen(str);

    assert(s);

    if (len < staticBufLen) {
        memcpy(staticBuf, s, len);
        staticBuf[len] = '\0';
        return staticBuf;
    }

    char *buf = Tiny_AllocUsingContext(thread, NULL, len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';

    return buf;
}

static Tiny_Value Lib_DictPut(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    char buf[1024];
    char *key = MakeNullTerminated(thread, buf, sizeof(buf), args[1]);

    DictSet(dict, key, &args[2]);

    if (key != buf) {
        Tiny_AllocUsingContext(thread, key, 0);
    }

    return Tiny_Null;
}

static Tiny_Value Lib_DictExists(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    char buf[1024];
    char *key = MakeNullTerminated(thread, buf, sizeof(buf), args[1]);

    const Tiny_Value *value = DictGet(dict, key);

    if (key != buf) {
        Tiny_AllocUsingContext(thread, key, 0);
    }

    return Tiny_NewBool(value);
}

static Tiny_Value Lib_DictGet(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    char buf[1024];
    char *key = MakeNullTerminated(thread, buf, sizeof(buf), args[1]);

    const Tiny_Value *value = DictGet(dict, key);

    if (key != buf) {
        Tiny_AllocUsingContext(thread, key, 0);
    }

    if (value) return *value;

    return Tiny_Null;
}

static Tiny_Value Lib_DictRemove(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    char buf[1024];
    char *key = MakeNullTerminated(thread, buf, sizeof(buf), args[1]);

    DictRemove(dict, key);

    if (key != buf) {
        Tiny_AllocUsingContext(thread, key, 0);
    }

    return Tiny_Null;
}

static Tiny_Value Lib_DictClear(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    DictClear(args[0].obj->nat.addr);
    return Tiny_Null;
}

static Tiny_Value Lib_DictKeys(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    Array *array = malloc(sizeof(Array));

    array->capacity = dict->filledCount;
    array->itemSize = sizeof(Tiny_Value);
    array->length = 0;
    array->data = malloc(sizeof(Tiny_Value) * dict->filledCount);

    for (int i = 0; i < dict->bucketCount; ++i) {
        const char *key = ArrayGetValue(&dict->keys, i, const char *);
        if (key) {
            *((Tiny_Value *)array->data + array->length) =
                Tiny_NewStringCopyNullTerminated(thread, key);
            array->length += 1;
        }
    }

    return Tiny_NewNative(thread, array, &ArrayProp);
}

static Tiny_Value Strcat(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    size_t totalLen = 0;

    for (int i = 0; i < count; ++i) {
        totalLen += Tiny_StringLen(args[i]);
    }

    char *newString = Tiny_AllocUsingContext(thread, NULL, totalLen + 1);
    char *ptr = newString;

    for (int i = 0; i < count; ++i) {
        size_t len = Tiny_StringLen(args[i]);
        memcpy(ptr, Tiny_ToString(args[i]), len);

        ptr += len;
    }

    newString[totalLen] = '\0';

    return Tiny_NewString(thread, newString, totalLen);
}

static Tiny_Value Lib_Substr(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    assert(count == 3);

    const char *s = Tiny_ToString(args[0]);
    size_t sLen = Tiny_StringLen(args[0]);

    int start = (int)Tiny_ToNumber(args[1]);
    int end = (int)Tiny_ToNumber(args[2]);

    if (end == -1) {
        end = sLen;
    }

    assert(start >= 0 && start <= end);

    if (start == end) {
        return Tiny_NewConstString("");
    }

    assert(end <= sLen);

    char *sub = Tiny_AllocUsingContext(thread, NULL, end - start + 1);
    for (int i = start; i < end; ++i) {
        sub[i - start] = s[i];
    }
    sub[end - start] = '\0';

    // TODO(Apaar): Figure out if there's a way we can just not allocate for this
    // TODO(Apaar): If strings are immutable and refcounted, we could just point
    // to the same string if we had some kind of "slice" primitive.
    return Tiny_NewString(thread, sub, (size_t)(end - start));
}

static Tiny_Value Lib_Ston(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    const char *str = Tiny_ToString(args[0]);
    float value = strtof(str, NULL);

    return Tiny_NewFloat(value);
}

static Tiny_Value Lib_Stoi(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    const char *str = Tiny_ToString(args[0]);
    int base = Tiny_ToInt(args[1]);

    long value = strtol(str, NULL, base);

    return Tiny_NewInt((int)value);
}

static Tiny_Value Lib_Ntos(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    float num = Tiny_ToNumber(args[0]);

    char buf[32];

    int c = snprintf(buf, sizeof(buf), "%g", num);

    return Tiny_NewStringCopy(thread, buf, c);
}

static Tiny_Value Lib_Time(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewInt((int)time(NULL));
}

static Tiny_Value SeedRand(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    srand((unsigned int)Tiny_ToInt(args[0]));
    return Tiny_Null;
}

static Tiny_Value Rand(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewInt(rand());
}

static Tiny_Value Lib_Input(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    if (count >= 1) printf("%s", Tiny_ToString(args[0]));

    char *buffer = Tiny_AllocUsingContext(thread, NULL, 8);
    size_t bufferLength = 0;
    size_t bufferCapacity = 8;

    int c = getc(stdin);
    int i = 0;

    while (c != '\n') {
        if (bufferLength + 1 >= bufferCapacity) {
            bufferCapacity *= 2;
            buffer = Tiny_AllocUsingContext(thread, buffer, bufferCapacity);
        }

        buffer[bufferLength++] = c;
        c = getc(stdin);
    }

    if (bufferLength + 1 >= bufferCapacity) {
        // Make room for null terminator
        buffer = Tiny_AllocUsingContext(thread, buffer, bufferCapacity + 1);
    }
    buffer[bufferLength] = '\0';

    return Tiny_NewString(thread, buffer, bufferLength);
}

static void Print(Tiny_Value val, bool repr) {
    switch (val.type) {
        case TINY_VAL_NULL:
            printf("<null>");
            break;
        case TINY_VAL_INT:
            printf("%i", val.i);
            break;
        case TINY_VAL_FLOAT:
            printf("%f", val.f);
            break;
        case TINY_VAL_CONST_STRING:
            if (repr) {
                printf("\"%s\"", val.cstr);
            } else {
                printf("%s", val.cstr);
            }
            break;
        case TINY_VAL_STRING:
            if (repr) {
                printf("\"%.*s\"", (int)val.obj->string.len, val.obj->string.ptr);
            } else {
                printf("%.*s", (int)val.obj->string.len, val.obj->string.ptr);
            }
            break;
        case TINY_VAL_LIGHT_NATIVE:
            printf("<light native at %p>", val.addr);
            break;
        case TINY_VAL_NATIVE: {
            if (repr && val.obj->nat.prop == &ArrayProp) {
                printf("[");

                const Array *array = val.obj->nat.addr;

                bool first = true;

                for (int i = 0; i < array->length; ++i) {
                    if (!first) {
                        printf(", ");
                    }
                    first = false;

                    Tiny_Value value = ArrayGetValue(array, i, Tiny_Value);

                    Print(value, true);
                }

                printf("]");
            } else if (val.obj->nat.prop && val.obj->nat.prop->name)
                printf("<native '%s' at %p>", val.obj->nat.prop->name, val.obj->nat.addr);
            else
                printf("<native at %p>", val.obj->nat.addr);
        } break;
        case TINY_VAL_STRUCT: {
            printf("struct {");

            for (int i = 0; i < val.obj->ostruct.n; ++i) {
                if (i > 0) {
                    printf(", ");
                }

                Print(val.obj->ostruct.fields[i], true);
            }

            putc('}', stdout);
        } break;
    }
}

static Tiny_Value Lib_Printf(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    const char *fmt = Tiny_ToString(args[0]);

    int arg = 1;

    while (*fmt) {
        if (*fmt == '%') {
            if (arg >= count) {
                fprintf(stderr, "Too few arguments for format '%s'\n", fmt);
                exit(1);
            }

            ++fmt;
            switch (*fmt) {
                case 'i':
                    printf("%d", args[arg].i);
                    break;
                case 'f':
                    printf("%f", args[arg].f);
                    break;
                case 's':
                    printf("%.*s", (int)Tiny_StringLen(args[arg]), Tiny_ToString(args[arg]));
                    break;
                case 'c':
                    printf("%c", args[arg].i);
                    break;

                case 'q':
                    Print(args[arg], true);
                    break;
                case '%':
                    putc('%', stdout);

                default:
                    printf("\nInvalid format specifier '%c'\n", *fmt);
            }
            ++fmt;
            ++arg;
        } else
            putc(*fmt++, stdout);
    }

    return Tiny_Null;
}

static Tiny_Value Exit(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    int arg = Tiny_ToInt(args[0]);

    exit(arg);

    return Tiny_Null;
}

static Tiny_Value Lib_Floor(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewFloat(floorf(Tiny_ToFloat(args[0])));
}

static Tiny_Value Lib_Ceil(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewFloat(ceilf(Tiny_ToFloat(args[0])));
}

#ifdef _WIN32
static Tiny_Value Lib_PerfCount(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    LARGE_INTEGER result;
    QueryPerformanceCounter(&result);

    return Tiny_NewInt((int)result.QuadPart);
}

static Tiny_Value Lib_PerfFreq(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    LARGE_INTEGER result;
    QueryPerformanceFrequency(&result);

    return Tiny_NewInt((int)result.QuadPart);
}

static Tiny_Value Lib_Sleep(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Sleep(Tiny_ToInt(args[0]));
    return Tiny_Null;
}
#endif

static Tiny_Value Lib_IntToI64(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    int64_t i = (int64_t)Tiny_ToInt(args[0]);

    // HACK(Apaar): Reinterpret as an address
    return Tiny_NewLightNative((void *)(intptr_t)i);
}

static Tiny_Value Lib_I64AddMany(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    int64_t sum = (int64_t)(intptr_t)Tiny_ToAddr(args[0]);

    for (int i = 1; i < count; ++i) {
        sum += (int64_t)(intptr_t)Tiny_ToAddr(args[i]);
    }

    return Tiny_NewLightNative((void *)(intptr_t)sum);
}

static Tiny_Value Lib_I64MulMany(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    int64_t product = (int64_t)(intptr_t)Tiny_ToAddr(args[0]);

    for (int i = 1; i < count; ++i) {
        product *= (int64_t)(intptr_t)Tiny_ToAddr(args[i]);
    }

    return Tiny_NewLightNative((void *)(intptr_t)product);
}

static Tiny_Value Lib_I64ToString(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    char buf[32] = {0};

    int len = snprintf(buf, sizeof(buf), "%ld", (int64_t)(intptr_t)Tiny_ToAddr(args[0]));

    return Tiny_NewStringCopy(thread, buf, len);
}

static Tiny_MacroResult BindJsonSerializerForType(Tiny_State *state, const Tiny_Symbol *sym) {
    if (sym->type == TINY_SYM_TAG_FOREIGN) {
        // For foreign types, it's up to the calling code to set up an appropriate serializer.
        // We can't do it automatically.
        //
        // e.g. If there was an array_str, then this assumes there's an "array_str_to_json".
        //
        // Basically %s_to_json
        return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
    }

    if (sym->type != TINY_SYM_TAG_STRUCT) {
        // It's a primitive type, we've already defined functions for all the types below.
        return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
    }

    // Hopefully enough lol?
    char buf[2048] = {0};
    int used = 0;

    snprintf(buf, sizeof(buf), "%s_to_json", sym->name);

    if (Tiny_FindFuncSymbol(state, buf)) {
        // Already bound, don't bother
        return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
    }

    used +=
        snprintf(buf, sizeof(buf), "func %s_to_json(v: %s): str {\n\treturn strcat(\"{\",\n\t\t",
                 sym->name, sym->name);

    for (int i = 0; i < Tiny_SymbolArrayCount(sym->sstruct.fields); ++i) {
        if (i > 0) {
            used += snprintf(buf + used, sizeof(buf) - used, ", \",\",\n\t\t");
        }

        const Tiny_Symbol *fieldSym = sym->sstruct.fields[i];

        BindJsonSerializerForType(state, fieldSym->fieldTag);

        used += snprintf(buf + used, sizeof(buf) - used, "\"\\\"%s\\\":\", %s_to_json(v.%s)",
                         fieldSym->name, fieldSym->fieldTag->name, fieldSym->name);
    }

    used += snprintf(buf + used, sizeof(buf) - used, ",\n\t\"}\")\n}");

    Tiny_CompileString(state, "(json mod)", buf);

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

static Tiny_MacroResult JsonModFunction(Tiny_State *state, char *const *args, int nargs,
                                        const char *asName) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify exactly 1 argument to 'use json_mod'",
        };
    }

    const Tiny_Symbol *sym = Tiny_FindTypeSymbol(state, args[0]);

    if (sym->type != TINY_SYM_TAG_STRUCT) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify struct type as argument to 'use json_mod'",
        };
    }

    return BindJsonSerializerForType(state, sym);

#if 0
    for (int i = 0; i < Tiny_SymbolArrayCount(sym->sstruct.fields); ++i) {
        const Tiny_Symbol *field = sym->sstruct.fields[i];
    }
#endif
}

static Tiny_MacroResult ArrayModFunction(Tiny_State *state, char *const *args, int nargs,
                                         const char *asName) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify exactly 1 argument to 'use array_mod'",
        };
    }

    if (!asName) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify an 'as' name when doing 'use array_mod'",
        };
    }

    if (!Tiny_FindTypeSymbol(state, args[0])) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "The array element type you specified does not exist",
        };
    }

    Tiny_RegisterType(state, asName);

    char sigbuf[512] = {0};

    snprintf(sigbuf, sizeof(sigbuf), "%s(...): %s", asName, asName);
    Tiny_BindFunction(state, sigbuf, CreateArray);

    snprintf(sigbuf, sizeof(sigbuf), "%s_clear(%s): void", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayClear);

    snprintf(sigbuf, sizeof(sigbuf), "%s_get(%s, int): %s", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayGet);

    snprintf(sigbuf, sizeof(sigbuf), "%s_push(%s, %s): void", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayPush);

    snprintf(sigbuf, sizeof(sigbuf), "%s_len(%s): int", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayPush);

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

void Tiny_BindStandardArray(Tiny_State *state) {
    Tiny_RegisterType(state, "array");

    Tiny_BindFunction(state, "array(...): array", CreateArray);
    Tiny_BindFunction(state, "array_clear(array): void", Lib_ArrayClear);
    Tiny_BindFunction(state, "array_resize(array, int): void", Lib_ArrayResize);
    Tiny_BindFunction(state, "array_get(array, int): any", Lib_ArrayGet);
    Tiny_BindFunction(state, "array_set(array, int, any): void", Lib_ArraySet);
    Tiny_BindFunction(state, "array_len(array): int", Lib_ArrayLen);
    Tiny_BindFunction(state, "array_push(array, any): void", Lib_ArrayPush);
    Tiny_BindFunction(state, "array_pop(array): any", Lib_ArrayPop);
    Tiny_BindFunction(state, "array_shift(array): any", Lib_ArrayShift);
    Tiny_BindFunction(state, "array_remove(array, int): void", Lib_ArrayRemove);

    // HACK(Apaar): Who needs generics when 90% of arrays are just string and int arrays?
    Tiny_RegisterType(state, "array_str");

    Tiny_BindFunction(state, "array_str(...): array_str", CreateArray);
    Tiny_BindFunction(state, "array_str_clear(array_str): void", Lib_ArrayClear);
    Tiny_BindFunction(state, "array_str_resize(array_str, int): void", Lib_ArrayResize);
    Tiny_BindFunction(state, "array_str_get(array_str, int): str", Lib_ArrayGet);
    Tiny_BindFunction(state, "array_str_set(array_str, int, str): void", Lib_ArraySet);
    Tiny_BindFunction(state, "array_str_len(array_str): int", Lib_ArrayLen);
    Tiny_BindFunction(state, "array_str_push(array_str, str): void", Lib_ArrayPush);
    Tiny_BindFunction(state, "array_str_pop(array_str): str", Lib_ArrayPop);
    Tiny_BindFunction(state, "array_str_shift(array_str): str", Lib_ArrayShift);
    Tiny_BindFunction(state, "array_str_remove(array, int): void", Lib_ArrayRemove);

    Tiny_BindMacro(state, "array_mod", ArrayModFunction);
}

void Tiny_BindStandardDict(Tiny_State *state) {
    Tiny_RegisterType(state, "array");
    Tiny_RegisterType(state, "dict");

    Tiny_BindFunction(state, "dict(...): dict", CreateDict);
    Tiny_BindFunction(state, "dict_put(dict, str, any): void", Lib_DictPut);
    Tiny_BindFunction(state, "dict_exists(dict, str): bool", Lib_DictExists);
    Tiny_BindFunction(state, "dict_get(dict, str): any", Lib_DictGet);
    Tiny_BindFunction(state, "dict_remove(dict, str): void", Lib_DictRemove);
    Tiny_BindFunction(state, "dict_keys(dict): array", Lib_DictKeys);
    Tiny_BindFunction(state, "dict_clear(dict): void", Lib_DictClear);

    Tiny_RegisterType(state, "dict_str_int");

    Tiny_BindFunction(state, "dict_str_int(...): dict_str_int", CreateDict);
    Tiny_BindFunction(state, "dict_str_int_put(dict_str_int, str, int): void", Lib_DictPut);
    Tiny_BindFunction(state, "dict_str_int_exists(dict_str_int, str): bool", Lib_DictExists);
    Tiny_BindFunction(state, "dict_str_int_get(dict_str_int, str): int", Lib_DictGet);
    Tiny_BindFunction(state, "dict_str_int_remove(dict_str_int, str): void", Lib_DictRemove);
    Tiny_BindFunction(state, "dict_str_int_keys(dict_str_int): array_str", Lib_DictKeys);
    Tiny_BindFunction(state, "dict_str_int_clear(dict_str_int): void", Lib_DictClear);
}

void Tiny_BindStandardIO(Tiny_State *state) {
    Tiny_RegisterType(state, "file");

    Tiny_BindFunction(state, "fopen(str, str): file", Lib_Fopen);
    Tiny_BindFunction(state, "fclose(file): void", Lib_Fclose);
    Tiny_BindFunction(state, "fread(file, int): str", Lib_Fread);
    Tiny_BindFunction(state, "fwrite(file, str, ...): void", Lib_Fwrite);
    Tiny_BindFunction(state, "fseek(file, int): void", Lib_Fseek);
    Tiny_BindFunction(state, "fsize(file): int", Lib_Fsize);

    Tiny_BindFunction(state, "read_file(str): str", Lib_ReadFile);
    Tiny_BindFunction(state, "write_file(str, str): bool", Lib_WriteFile);

    Tiny_BindFunction(state, "input(...): void", Lib_Input);
    Tiny_BindFunction(state, "printf(str, ...): void", Lib_Printf);
}

void Tiny_BindI64(Tiny_State *state) {
    Tiny_RegisterType(state, "i64");

    Tiny_BindFunction(state, "int_to_i64(int): i64", Lib_IntToI64);
    Tiny_BindFunction(state, "i64_add_many(i64, ...): i64", Lib_I64AddMany);
    Tiny_BindFunction(state, "i64_mul_many(i64, ...): i64", Lib_I64MulMany);
    Tiny_BindFunction(state, "i64_to_string(i64): str", Lib_I64ToString);
}

static TINY_FOREIGN_FUNCTION(Lib_PrimitiveToJson) {
    assert(count == 1);

    switch (args[0].type) {
        case TINY_VAL_NULL:
            return Tiny_NewConstString("null");
        case TINY_VAL_BOOL:
            return Tiny_NewConstString(args[0].boolean ? "true" : "false");
        case TINY_VAL_FLOAT:
        case TINY_VAL_INT:
            return Lib_Ntos(thread, args, count);
        case TINY_VAL_CONST_STRING:
        case TINY_VAL_STRING: {
            // TODO(Apaar): Escape args[0] string
            Tiny_Value forwardArgs[] = {Tiny_NewConstString("\""), args[0],
                                        Tiny_NewConstString("\"")};

            return Strcat(thread, forwardArgs, sizeof(forwardArgs));
        } break;

        default:
            // TODO(Apaar): Panic in this scenario
            return Tiny_NewConstString("null");
    }
}

static Tiny_MacroResult DelegateMacroFunction(Tiny_State *state, char *const *args, int nargs,
                                              const char *asName) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify exactly 1 argument to 'use delegate'",
        };
    }

    if (!asName) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .errorMessage = "Must specify an 'as' name when doing 'use delegate'",
        };
    }

    char typeBuf[512] = {0};
    char argsBuf[512] = {0};
    char paramsBuf[512] = {0};

    int typeUsed = 0;
    int argsUsed = 0;
    int paramsUsed = 0;

    typeUsed += snprintf(typeBuf, sizeof(typeBuf), "delegate_");

    const Tiny_Symbol *func = Tiny_FindFuncSymbol(state, args[0]);

    for (int i = 0;
         i < Tiny_SymbolArrayCount(func->type == TINY_SYM_FUNCTION ? func->func.args
                                                                   : func->foreignFunc.argTags);
         ++i) {
        if (i > 0) {
            typeUsed += snprintf(typeBuf + typeUsed, sizeof(typeBuf) - typeUsed, "_");
            argsUsed += snprintf(argsBuf + argsUsed, sizeof(argsBuf) - argsUsed, ", ");
            paramsUsed += snprintf(paramsBuf + paramsUsed, sizeof(paramsBuf) - paramsUsed, ", ");
        }

        const Tiny_Symbol *argTag = func->type == TINY_SYM_FUNCTION ? func->func.args[i]->var.tag
                                                                    : func->foreignFunc.argTags[i];

        typeUsed += snprintf(typeBuf + typeUsed, sizeof(typeBuf) - typeUsed, "%s", argTag->name);
        argsUsed +=
            snprintf(argsBuf + argsUsed, sizeof(argsBuf) - argsUsed, "a%d: %s", i, argTag->name);
        paramsUsed += snprintf(paramsBuf + paramsUsed, sizeof(paramsBuf) - paramsUsed, "a%d", i);
    }

    const Tiny_Symbol *returnTag =
        func->type == TINY_SYM_FUNCTION ? func->func.returnTag : func->foreignFunc.returnTag;

    const char *returnTagName = returnTag->name;

    typeUsed += snprintf(typeBuf + typeUsed, sizeof(typeBuf) - typeUsed, "_%s", returnTagName);

    if (!Tiny_FindTypeSymbol(state, typeBuf)) {
        Tiny_RegisterType(state, typeBuf);

        char callBuf[512] = {0};

        if (returnTag->type != TINY_SYM_TAG_VOID) {
            snprintf(callBuf, sizeof(callBuf),
                     "func %s_call(f: %s, %s): %s { return call_function(f, %s) }", typeBuf,
                     typeBuf, argsBuf, returnTagName, paramsBuf);
        } else {
            snprintf(callBuf, sizeof(callBuf), "func %s_call(f: %s, %s) { call_function(f, %s) }",
                     typeBuf, typeBuf, argsBuf, paramsBuf);
        }

        Tiny_CompileString(state, "(delegate call function)", callBuf);
    }

    if (!Tiny_FindFuncSymbol(state, asName)) {
        char createBuf[512] = {0};
        snprintf(createBuf, sizeof(createBuf),
                 "func %s(): %s { return get_function_index(\"%s\") }", asName, typeBuf, args[0]);

        Tiny_CompileString(state, "(delegate create function", createBuf);
    }

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

static TINY_FOREIGN_FUNCTION(GetFunctionIndex) {
    assert(count == 1);

    int i = Tiny_GetFunctionIndex(thread->state, Tiny_ToString(args[0]));
    return Tiny_NewInt(i);
}

static TINY_FOREIGN_FUNCTION(CallFunction) {
    assert(count >= 1);
    int i = Tiny_ToInt(args[0]);

    return Tiny_CallFunction(thread, i, args + 1, count - 1);
}

void Tiny_BindStandardLib(Tiny_State *state) {
    Tiny_BindConstInt(state, "INT_MAX", INT_MAX);

    Tiny_BindFunction(state, "strlen(str): int", Strlen);
    Tiny_BindFunction(state, "stridx(str, int): int", Stridx);
    Tiny_BindFunction(state, "strchr(str, int): int", Strchr);
    Tiny_BindFunction(state, "strcat(str, str, ...): str", Strcat);
    Tiny_BindFunction(state, "substr(str, int, int): str", Lib_Substr);

    Tiny_BindFunction(state, "ston(str): float", Lib_Ston);
    Tiny_BindFunction(state, "ntos(...): str", Lib_Ntos);

    Tiny_BindFunction(state, "stoi(str, int): int", Lib_Stoi);

    Tiny_BindFunction(state, "time(): int", Lib_Time);
    Tiny_BindFunction(state, "srand(int): void", SeedRand);
    Tiny_BindFunction(state, "rand(): int", Rand);

    Tiny_BindFunction(state, "floor(float): float", Lib_Floor);
    Tiny_BindFunction(state, "ceil(float): float", Lib_Ceil);

    // HACK(Apaar): These are labeled as returning/taking any to facilitate the conversion to
    // a delegate
    Tiny_BindFunction(state, "get_function_index(str): any", GetFunctionIndex);
    Tiny_BindFunction(state, "call_function(any, ...): any", CallFunction);

    Tiny_BindMacro(state, "delegate", DelegateMacroFunction);

#ifdef _WIN32
    Tiny_BindFunction(state, "perf_count(): int", Lib_PerfCount);
    Tiny_BindFunction(state, "perf_freq(): int", Lib_PerfFreq);
    Tiny_BindFunction(state, "sleep(int): void", Lib_Sleep);
#endif

    Tiny_BindFunction(state, "exit(int): void", Exit);

    // TODO(Apaar): Move these out of the standard lib
    Tiny_BindFunction(state, "bool_to_json", Lib_PrimitiveToJson);
    Tiny_BindFunction(state, "str_to_json", Lib_PrimitiveToJson);
    Tiny_BindFunction(state, "int_to_json", Lib_PrimitiveToJson);
    Tiny_BindFunction(state, "float_to_json", Lib_PrimitiveToJson);

    Tiny_BindMacro(state, "json_mod", JsonModFunction);
}

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
    char *str = Tiny_AllocUsingContext(thread->ctx, NULL, num + 1);

    int readCount = fread(str, 1, num, file);

    if (readCount < 0) {
        Tiny_AllocUsingContext(thread->ctx, str, 0);
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

    char *s = Tiny_AllocUsingContext(thread->ctx, NULL, len + 1);
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

static void ArrayFree(Tiny_Context *ctx, void *ptr) {
    Array *array = ptr;

    DestroyArray(array);
    Tiny_AllocUsingContext(*ctx, array, 0);
}

static void ArrayMark(void *ptr) {
    Array *array = ptr;

    for (int i = 0; i < ArrayLen(array); ++i) {
        Tiny_ProtectFromGC(*ArrayGet(array, i));
    }
}

const Tiny_NativeProp ArrayProp = {
    "array",
    ArrayMark,
    ArrayFree,
};

static TINY_FOREIGN_FUNCTION(CreateArray) {
    Array *array = Tiny_AllocUsingContext(thread->ctx, NULL, sizeof(Array));

    InitArrayEx(array, thread->ctx, count, args);

    memcpy(array->data, args, sizeof(Tiny_Value) * count);

    return Tiny_NewNative(thread, array, &ArrayProp);
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayLen) {
    Array *array = Tiny_ToAddr(args[0]);

    return Tiny_NewInt(ArrayLen(array));
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayClear) {
    Array *array = Tiny_ToAddr(args[0]);
    ArrayClear(array);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayResize) {
    Array *array = Tiny_ToAddr(args[0]);
    ArrayResize(array, (int)Tiny_ToNumber(args[0]), Tiny_Null);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayPush) {
    Array *array = Tiny_ToAddr(args[0]);
    Tiny_Value value = args[1];

    ArrayPush(array, value);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(Lib_ArrayGet) {
    Array *array = Tiny_ToAddr(args[0]);
    int index = Tiny_ToInt(args[1]);

    return *ArrayGet(array, index);
}

static Tiny_Value Lib_ArraySet(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Array *array = Tiny_ToAddr(args[0]);
    int index = Tiny_ToInt(args[1]);
    Tiny_Value value = args[2];

    ArraySet(array, index, value);

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

static int CompareInts(const void *aRaw, const void *bRaw) {
    const Tiny_Value *a = aRaw;
    const Tiny_Value *b = bRaw;

    assert(a->type == TINY_VAL_INT);
    assert(b->type == TINY_VAL_INT);

    return a->i - b->i;
}

static TINY_FOREIGN_FUNCTION(Lib_ArraySortInt) {
    Array *array = Tiny_ToAddr(args[0]);

    qsort(array->data, ArrayLen(array), sizeof(Tiny_Value), CompareInts);

    return Tiny_Null;
}

static void DictProtectFromGC(void *p) {
    Dict *d = p;

    for (int i = 0; i < d->bucketCount; ++i) {
        Tiny_Value key = *ArrayGet(&d->keys, i);
        if (!Tiny_IsNull(key)) {
            Tiny_ProtectFromGC(key);
            Tiny_ProtectFromGC(*ArrayGet(&d->values, i));
        }
    }
}

static void DictFree(Tiny_Context *ctx, void *d) {
    DestroyDict(d);
    // Free the pointer itself
    Tiny_AllocUsingContext(*ctx, d, 0);
}

const Tiny_NativeProp DictProp = {
    "dict",
    DictProtectFromGC,
    DictFree,
};

static Tiny_Value CreateDict(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = Tiny_AllocUsingContext(thread->ctx, NULL, sizeof(Dict));

    InitDict(dict, thread->ctx);

    if (count > 0 && count % 2 != 0) {
        fprintf(stderr,
                "Expected even number of arguments to dict(...) (since each key "
                "needs a corresponding value) but got %d.\n",
                count);
        exit(1);
    }

    for (int i = 0; i < count; i += 2) DictSet(dict, args[i], args[i + 1]);

    return Tiny_NewNative(thread, dict, &DictProp);
}

static Tiny_Value Lib_DictPut(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;
    DictSet(dict, args[1], args[2]);

    return Tiny_Null;
}

static Tiny_Value Lib_DictExists(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    const Tiny_Value *value = DictGet(dict, args[1]);

    return Tiny_NewBool(value);
}

static Tiny_Value Lib_DictGet(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;
    const Tiny_Value *value = DictGet(dict, args[1]);

    if (value) return *value;

    return Tiny_Null;
}

static Tiny_Value Lib_DictRemove(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;
    DictRemove(dict, args[1]);

    return Tiny_Null;
}

static Tiny_Value Lib_DictClear(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    DictClear(args[0].obj->nat.addr);
    return Tiny_Null;
}

static Tiny_Value Lib_DictKeys(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    Dict *dict = args[0].obj->nat.addr;

    Array *array = Tiny_AllocUsingContext(thread->ctx, NULL, sizeof(Array));

    InitArray(array, thread->ctx);

    for (int i = 0; i < dict->bucketCount; ++i) {
        Tiny_Value key = *ArrayGet(&dict->keys, i);

        if (!Tiny_IsNull(key)) {
            ArrayPush(array, key);
        }
    }

    return Tiny_NewNative(thread, array, &ArrayProp);
}

static Tiny_Value Strcat(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    size_t totalLen = 0;

    for (int i = 0; i < count; ++i) {
        totalLen += Tiny_StringLen(args[i]);
    }

    char *newString = Tiny_AllocUsingContext(thread->ctx, NULL, totalLen + 1);
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

    char *sub = Tiny_AllocUsingContext(thread->ctx, NULL, end - start + 1);
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
    int base = count > 1 ? Tiny_ToInt(args[1]) : 10;

    long value = strtol(str, NULL, base);

    return Tiny_NewInt((int)value);
}

static Tiny_Value Lib_Ntos(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    float num = Tiny_ToNumber(args[0]);

    char buf[32];

    int c = snprintf(buf, sizeof(buf), "%g", num);

    return Tiny_NewStringCopy(thread, buf, c);
}

static TINY_FOREIGN_FUNCTION(Lib_IntToStr) {
    Tiny_Int i = Tiny_ToInt(args[0]);

    char buf[32] = {0};

    int len = snprintf(buf, sizeof(buf), "%lld", (int64_t)i);

    return Tiny_NewStringCopy(thread, buf, len);
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

    char *buffer = Tiny_AllocUsingContext(thread->ctx, NULL, 8);
    size_t bufferLength = 0;
    size_t bufferCapacity = 8;

    int c = getc(stdin);
    int i = 0;

    while (c != '\n') {
        if (bufferLength + 1 >= bufferCapacity) {
            bufferCapacity *= 2;
            buffer = Tiny_AllocUsingContext(thread->ctx, buffer, bufferCapacity);
        }

        buffer[bufferLength++] = c;
        c = getc(stdin);
    }

    if (bufferLength + 1 >= bufferCapacity) {
        // Make room for null terminator
        buffer = Tiny_AllocUsingContext(thread->ctx, buffer, bufferCapacity + 1);
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

                Array *array = val.obj->nat.addr;

                bool first = true;

                for (int i = 0; i < ArrayLen(array); ++i) {
                    if (!first) {
                        printf(", ");
                    }
                    first = false;

                    Tiny_Value value = *ArrayGet(array, i);

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

    int len = snprintf(buf, sizeof(buf), "%lld", (int64_t)(intptr_t)Tiny_ToAddr(args[0]));

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

    Tiny_CompileResult compileResult = Tiny_CompileString(state, "(json mod)", buf);

    if (compileResult.type != TINY_COMPILE_SUCCESS) {
        Tiny_MacroResult macroResult = {.type = TINY_MACRO_ERROR};

        snprintf(macroResult.error.msg, sizeof(macroResult.error.msg),
                 "Failed to compile JSON code: %s", compileResult.error.msg);

        return macroResult;
    }

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

static TINY_MACRO_FUNCTION(JsonMacroFunction) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify exactly 1 argument to 'use json'",
        };
    }

    const Tiny_Symbol *sym = Tiny_FindTypeSymbol(state, args[0]);

    if (sym->type != TINY_SYM_TAG_STRUCT) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify struct type as argument to 'use json_mod'",
        };
    }

    return BindJsonSerializerForType(state, sym);
}

static TINY_MACRO_FUNCTION(ArrayMacroFunction) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify exactly 1 argument to 'use array'",
        };
    }

    if (!asName) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify an 'as' name when doing 'use array'",
        };
    }

    const Tiny_Symbol *elemType = Tiny_FindTypeSymbol(state, args[0]);

    if (!elemType) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "The array element type you specified does not exist",
        };
    }

    Tiny_RegisterType(state, asName);

    char sigbuf[512] = {0};

    snprintf(sigbuf, sizeof(sigbuf), "%s(...): %s", asName, asName);
    Tiny_BindFunction(state, sigbuf, CreateArray);

    snprintf(sigbuf, sizeof(sigbuf), "%s_clear(%s): void", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayClear);

    snprintf(sigbuf, sizeof(sigbuf), "%s_resize(%s, int): void", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayResize);

    snprintf(sigbuf, sizeof(sigbuf), "%s_get(%s, int): %s", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayGet);

    // Conform to the array index syntax
    snprintf(sigbuf, sizeof(sigbuf), "%s_get_index(%s, int): %s", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayGet);

    snprintf(sigbuf, sizeof(sigbuf), "%s_set(%s, int, %s): void", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArraySet);

    // Conform to the array index syntax
    snprintf(sigbuf, sizeof(sigbuf), "%s_set_index(%s, int, %s): void", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArraySet);

    snprintf(sigbuf, sizeof(sigbuf), "%s_len(%s): int", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayLen);

    snprintf(sigbuf, sizeof(sigbuf), "%s_push(%s, %s): void", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayPush);

    snprintf(sigbuf, sizeof(sigbuf), "%s_pop(%s): %s", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayPop);

    snprintf(sigbuf, sizeof(sigbuf), "%s_shift(%s): %s", asName, asName, args[0]);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayShift);

    snprintf(sigbuf, sizeof(sigbuf), "%s_remove(%s, int): void", asName, asName);
    Tiny_BindFunction(state, sigbuf, Lib_ArrayRemove);

    if (elemType->type == TINY_SYM_TAG_INT) {
        // TODO(Apaar): Add functions to sort other types
        snprintf(sigbuf, sizeof(sigbuf), "%s_sort(%s): void", asName, asName);
        Tiny_BindFunction(state, sigbuf, Lib_ArraySortInt);
    }

    return (Tiny_MacroResult){.type = TINY_MACRO_SUCCESS};
}

void Tiny_BindStandardArray(Tiny_State *state) {
    Tiny_BindMacro(state, "array", ArrayMacroFunction);
}

void Tiny_BindStandardDict(Tiny_State *state) {
    Tiny_RegisterType(state, "array_str");
    Tiny_RegisterType(state, "dict");

    Tiny_BindFunction(state, "dict(...): dict", CreateDict);
    Tiny_BindFunction(state, "dict_put(dict, str, any): void", Lib_DictPut);
    Tiny_BindFunction(state, "dict_exists(dict, str): bool", Lib_DictExists);
    Tiny_BindFunction(state, "dict_get(dict, str): any", Lib_DictGet);
    Tiny_BindFunction(state, "dict_remove(dict, str): void", Lib_DictRemove);
    Tiny_BindFunction(state, "dict_keys(dict): array_str", Lib_DictKeys);
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

    Tiny_BindFunction(state, "input(...): str", Lib_Input);
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

static TINY_MACRO_FUNCTION(DelegateMacroFunction) {
    if (nargs != 1) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify exactly 1 argument to 'use delegate'",
        };
    }

    if (!asName) {
        return (Tiny_MacroResult){
            .type = TINY_MACRO_ERROR,
            .error.msg = "Must specify an 'as' name when doing 'use delegate'",
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
                     "func %s_call(f: %s, %s): %s { return cast(call_function(f, %s), %s) }",
                     typeBuf, typeBuf, argsBuf, returnTagName, paramsBuf, returnTagName);
        } else {
            snprintf(callBuf, sizeof(callBuf), "func %s_call(f: %s, %s) { call_function(f, %s) }",
                     typeBuf, typeBuf, argsBuf, paramsBuf);
        }

        Tiny_CompileString(state, "(delegate call function)", callBuf);
    }

    if (!Tiny_FindFuncSymbol(state, asName)) {
        char createBuf[512] = {0};
        snprintf(createBuf, sizeof(createBuf),
                 "func %s(): %s { return cast(get_function_index(\"%s\"), %s) }", asName, typeBuf,
                 args[0], typeBuf);

        Tiny_CompileString(state, "(delegate create function)", createBuf);
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

static TINY_FOREIGN_FUNCTION(GetExecutingLine) {
    int line = 0;
    Tiny_GetExecutingFileLine(thread, NULL, &line);

    return Tiny_NewInt(line);
}

void Tiny_BindStandardLib(Tiny_State *state) {
    Tiny_BindConstInt(state, "INT_MAX", INT_MAX);

    Tiny_BindFunction(state, "strlen(str): int", Strlen);
    Tiny_BindFunction(state, "stridx(str, int): int", Stridx);
    Tiny_BindFunction(state, "sget(str, int): int", Stridx);
    Tiny_BindFunction(state, "strchr(str, int): int", Strchr);
    Tiny_BindFunction(state, "strcat(str, str, ...): str", Strcat);
    Tiny_BindFunction(state, "substr(str, int, int): str", Lib_Substr);

    Tiny_BindFunction(state, "ston(str): float", Lib_Ston);
    Tiny_BindFunction(state, "str_to_int(str): int", Lib_Stoi);
    Tiny_BindFunction(state, "int_to_str(int): str", Lib_IntToStr);
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

    Tiny_BindFunction(state, "get_executing_line", GetExecutingLine);

    Tiny_BindMacro(state, "json", JsonMacroFunction);
}

// tiny.c -- an bytecode-based interpreter for the tiny language
#include "tiny.h"

#include <assert.h>
#include <ctype.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detail.h"
#include "expr.h"
#include "lexer.h"
#include "opcodes.h"
#include "stretchy_buffer.h"
#include "util.h"

#ifndef UCHAR_MAX
#define UCHAR_MAX 255
#endif

// TODO(Apaar): Make a cstr type (for ConstStrings)
// all str's can be used where a cstr is expected, but
// cstrs cannot be used where strs are expected.
// This'll catch potential string lifetime issues at
// compile-time.

const Tiny_Value Tiny_Null = {TINY_VAL_NULL};

// In the event that you want to declare a symbol that can't be named
const char *ANON_SYM_NAME = "(anonymous)";

static void *DefaultAlloc(void *ptr, size_t size, void *userdata) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    return realloc(ptr, size);
}

Tiny_Context Tiny_DefaultContext = {DefaultAlloc, NULL};

static void ReportErrorL(Tiny_State *state, Tiny_Lexer *l, const char *s, ...) {
    va_list args;
    va_start(args, s);

    state->compileErrorResult.type = TINY_COMPILE_ERROR;

    Tiny_FormatErrorV(state->compileErrorResult.error.msg,
                      sizeof(state->compileErrorResult.error.msg), l->fileName, l->src, l->pos, s,
                      args);

    va_end(args);

    longjmp(state->compileErrorJmpBufs[state->compileCallNestCount - 1], 1);
}

static void ReportErrorSL(Tiny_State *state, const char *s, ...) {
    va_list args;
    va_start(args, s);

    state->compileErrorResult.type = TINY_COMPILE_ERROR;

    Tiny_FormatErrorV(state->compileErrorResult.error.msg,
                      sizeof(state->compileErrorResult.error.msg), state->l.fileName, state->l.src,
                      state->l.pos, s, args);

    va_end(args);

    longjmp(state->compileErrorJmpBufs[state->compileCallNestCount - 1], 1);
}

static char *CloneString(Tiny_Context *ctx, const char *str) {
    size_t len = strlen(str);

    char *dup = TMalloc(ctx, len + 1);
    memcpy(dup, str, len + 1);

    return dup;
}

// Create a string node specifically for being put inside an expr (i.e. allocated in the parser
// arena)
static Tiny_StringNode *CreateExprStringNode(Tiny_State *state, const char *str) {
    size_t len = strlen(str);

    Tiny_StringNode *node =
        Tiny_ArenaAlloc(&state->parserArena, sizeof(Tiny_StringNode) + len + 1, sizeof(void *));
    char *dup = (char *)node + sizeof(Tiny_StringNode);

    memcpy(dup, str, len + 1);
    node->value = dup;
    node->next = NULL;

    return node;
}

static void DeleteObject(Tiny_Context *ctx, Tiny_Object *obj) {
    if (obj->type == TINY_VAL_STRING) {
        char *internalStr = (char *)obj + sizeof(Tiny_Object);

        if (obj->string.ptr == internalStr) {
            // FIXME(Apaar): Is it possible for there to be a string which just happens to be
            // allocated after the object pointer?

            // If the string pointer is directly after the object then it is part of the object
            // allocation and does not need to be freed separately
        } else {
            TFree(ctx, obj->string.ptr);
        }
    } else if (obj->type == TINY_VAL_NATIVE) {
        if (obj->nat.prop && obj->nat.prop->finalize) {
            obj->nat.prop->finalize(ctx, obj->nat.addr);
        }
    }

    TFree(ctx, obj);
}

static inline bool IsObject(Tiny_Value val) {
    return val.type == TINY_VAL_STRING || val.type == TINY_VAL_NATIVE ||
           val.type == TINY_VAL_STRUCT;
}

void Tiny_ProtectFromGC(Tiny_Value value) {
    if (!IsObject(value)) return;

    Tiny_Object *obj = value.obj;

    assert(obj);

    if (obj->marked) return;

    if (obj->type == TINY_VAL_NATIVE) {
        if (obj->nat.prop && obj->nat.prop->protectFromGC)
            obj->nat.prop->protectFromGC(obj->nat.addr);
    } else if (obj->type == TINY_VAL_STRUCT) {
        for (int i = 0; i < obj->ostruct.n; ++i) Tiny_ProtectFromGC(obj->ostruct.fields[i]);
    }

    obj->marked = 1;
}

static void MarkAll(Tiny_StateThread *thread);

static void Sweep(Tiny_StateThread *thread) {
    Tiny_Object **object = &thread->gcHead;
    while (*object) {
        if (!(*object)->marked) {
            Tiny_Object *unreached = *object;
            --thread->numObjects;
            *object = unreached->next;
            DeleteObject(&thread->ctx, unreached);
        } else {
            (*object)->marked = 0;
            object = &(*object)->next;
        }
    }
}

static void GarbageCollect(Tiny_StateThread *thread) {
    MarkAll(thread);
    Sweep(thread);
    thread->maxNumObjects = thread->numObjects * 2;
}

const char *Tiny_ToString(const Tiny_Value value) {
    if (value.type == TINY_VAL_CONST_STRING) return value.cstr;
    if (value.type != TINY_VAL_STRING) return NULL;

    return value.obj->string.ptr;
}

size_t Tiny_StringLen(const Tiny_Value value) {
    if (value.type == TINY_VAL_CONST_STRING) return strlen(value.cstr);
    if (value.type != TINY_VAL_STRING) return 0;

    return value.obj->string.len;
}

void *Tiny_ToAddr(const Tiny_Value value) {
    if (value.type == TINY_VAL_LIGHT_NATIVE) return value.addr;
    if (value.type != TINY_VAL_NATIVE) return NULL;

    return value.obj->nat.addr;
}

const Tiny_NativeProp *Tiny_GetProp(const Tiny_Value value) {
    if (value.type != TINY_VAL_NATIVE) return NULL;
    return value.obj->nat.prop;
}

Tiny_Value Tiny_GetField(const Tiny_Value value, int index) {
    if (value.type != TINY_VAL_STRUCT) return Tiny_Null;
    assert(index >= 0 && index < value.obj->ostruct.n);

    return value.obj->ostruct.fields[index];
}

bool Tiny_AreValuesEqual(Tiny_Value a, Tiny_Value b) {
    bool bothStrings = ((a.type == TINY_VAL_CONST_STRING && b.type == TINY_VAL_STRING) ||
                        (a.type == TINY_VAL_STRING && b.type == TINY_VAL_CONST_STRING));

    if (a.type != b.type && !bothStrings) {
        return false;
    }

    if (a.type == TINY_VAL_NULL) {
        return true;
    }

    if (a.type == TINY_VAL_BOOL) {
        return a.boolean == b.boolean;
    }

    if (a.type == TINY_VAL_INT) {
        return a.i == b.i;
    }

    if (a.type == TINY_VAL_FLOAT) {
        return a.f == b.f;
    }

    if (a.type == TINY_VAL_NATIVE) {
        return a.obj->nat.addr == b.obj->nat.addr;
    }

    if (a.type == TINY_VAL_LIGHT_NATIVE) {
        return a.addr == b.addr;
    }

    if (a.type == TINY_VAL_STRUCT) {
        return a.obj == b.obj;
    }

    if (a.type == TINY_VAL_STRING) {
        size_t aLen = Tiny_StringLen(a);
        size_t bLen = Tiny_StringLen(b);

        if (aLen != bLen) {
            return false;
        }

        return strncmp(a.obj->string.ptr, Tiny_ToString(b), aLen) == 0;
    }

    if (a.type == TINY_VAL_CONST_STRING) {
        if (b.type == TINY_VAL_CONST_STRING && a.cstr == b.cstr) {
            return true;
        }

        size_t aLen = Tiny_StringLen(a);
        size_t bLen = Tiny_StringLen(b);

        if (aLen != bLen) {
            return false;
        }

        return strncmp(a.cstr, Tiny_ToString(b), aLen) == 0;
    }

    return false;
}

static Tiny_Object *NewObject(Tiny_StateThread *thread, Tiny_ValueType type, size_t extra) {
    Tiny_Object *obj = TMalloc(&thread->ctx, sizeof(Tiny_Object) + extra);

    obj->type = type;
    obj->next = thread->gcHead;
    thread->gcHead = obj;
    obj->marked = 0;

    thread->numObjects++;

    return obj;
}

// Allocates memory for and copies the given string contiguously with the object, resulting in
// a single allocation for the entire thing.
static Tiny_Object *NewStringObjectEmbedString(Tiny_StateThread *thread, const char *str,
                                               size_t len) {
    Tiny_Object *obj = NewObject(thread, TINY_VAL_STRING, len + 1);

    obj->string.len = len;
    obj->string.ptr = (char *)obj + sizeof(Tiny_Object);
    memcpy(obj->string.ptr, str, len);

    // Null terminate the string for interfacing with C
    obj->string.ptr[obj->string.len] = '\0';

    return obj;
}

static Tiny_Object *NewStructObject(Tiny_StateThread *thread, Word n) {
    assert(n >= 0);

    Tiny_Object *obj = NewObject(thread, TINY_VAL_STRUCT, sizeof(Tiny_Value) * n);
    memset(obj->ostruct.fields, 0, sizeof(Tiny_Value) * n);

    obj->ostruct.n = n;

    thread->numObjects++;

    return obj;
}

Tiny_Value Tiny_NewLightNative(void *ptr) {
    Tiny_Value val;

    val.type = TINY_VAL_LIGHT_NATIVE;
    val.addr = ptr;

    return val;
}

Tiny_Value Tiny_NewNative(Tiny_StateThread *thread, void *ptr, const Tiny_NativeProp *prop) {
    assert(thread && thread->state);

    // Make sure thread is alive
    assert(thread->pc >= 0);

    Tiny_Object *obj = NewObject(thread, TINY_VAL_NATIVE, 0);

    obj->nat.addr = ptr;
    obj->nat.prop = prop;

    Tiny_Value val;

    val.type = TINY_VAL_NATIVE;
    val.obj = obj;

    return val;
}

Tiny_Value Tiny_NewBool(bool value) {
    Tiny_Value val;

    val.type = TINY_VAL_BOOL;
    val.boolean = value;

    return val;
}

Tiny_Value Tiny_NewInt(Tiny_Int i) {
    Tiny_Value val;

    val.type = TINY_VAL_INT;
    val.i = i;

    return val;
}

Tiny_Value Tiny_NewFloat(Tiny_Float f) {
    Tiny_Value val;

    val.type = TINY_VAL_FLOAT;
    val.f = f;

    return val;
}

Tiny_Value Tiny_NewConstString(const char *str) {
    assert(str);

    Tiny_Value val;

    val.type = TINY_VAL_CONST_STRING;
    val.cstr = str;

    return val;
}

// This assumes the given char* was allocated using Tiny_AllocUsingContext or equivalent.
// It takes ownership of the char*, avoiding any intermediate copies.
Tiny_Value Tiny_NewString(Tiny_StateThread *thread, char *str, size_t len) {
    assert(thread && thread->state && str);

    Tiny_Object *obj = NewObject(thread, TINY_VAL_STRING, 0);

    obj->string.len = len;
    obj->string.ptr = str;

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    return val;
}

// This is equivalent to Tiny_NewString but it figures out the length assuming
// the given pointer is null-terminated.
Tiny_Value Tiny_NewStringNullTerminated(Tiny_StateThread *thread, char *str) {
    return Tiny_NewString(thread, str, strlen(str));
}

// Same as Tiny_NewString but it allocate memory for and copies the given string.
Tiny_Value Tiny_NewStringCopy(Tiny_StateThread *thread, const char *src, size_t len) {
    assert(thread && thread->state && src);

    Tiny_Object *obj = NewStringObjectEmbedString(thread, src, len);

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    return val;
}

// Same as Tiny_NewStringCopy but assumes the given string is null terminated.
Tiny_Value Tiny_NewStringCopyNullTerminated(Tiny_StateThread *thread, const char *src) {
    return Tiny_NewStringCopy(thread, src, strlen(src));
}

static void Symbol_destroy(Tiny_Symbol *sym, Tiny_Context *ctx);

static Tiny_Value Lib_ToInt(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewInt((Tiny_Int)Tiny_ToFloat(args[0]));
}

static Tiny_Value Lib_ToFloat(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewFloat((Tiny_Float)Tiny_ToInt(args[0]));
}

Tiny_State *Tiny_CreateStateWithContext(Tiny_Context ctx) {
    Tiny_State *state = TMalloc(&ctx, sizeof(Tiny_State));

    state->l = (Tiny_Lexer){0};

    state->ctx = ctx;

    state->program = NULL;
    state->numGlobalVars = 0;

    state->numStrings = 0;

    state->numFunctions = 0;
    state->functionPcs = NULL;

    state->numForeignFunctions = 0;
    state->foreignFunctions = NULL;

    state->currScope = 0;
    state->currFunc = NULL;
    state->globalSymbols = NULL;

    state->pcToFileLine = NULL;

    state->compileCallNestCount = 0;

    Tiny_BindFunction(state, "int(float): int", Lib_ToInt);
    Tiny_BindFunction(state, "float(int): float", Lib_ToFloat);

    return state;
}

Tiny_State *Tiny_CreateState(void) { return Tiny_CreateStateWithContext(Tiny_DefaultContext); }

void Tiny_DeleteState(Tiny_State *state) {
    sb_free(&state->ctx, state->program);

    // Delete all the Strings
    for (int i = 0; i < state->numStrings; ++i) {
        TFree(&state->ctx, state->strings[i]);
    }

    // Delete all symbols
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol_destroy(state->globalSymbols[i], &state->ctx);
    }

    sb_free(&state->ctx, state->globalSymbols);

    // Reset function and variable data
    TFree(&state->ctx, state->functionPcs);
    TFree(&state->ctx, state->foreignFunctions);

    sb_free(&state->ctx, state->pcToFileLine);

    TFree(&state->ctx, state);
}

void Tiny_InitThreadWithContext(Tiny_StateThread *thread, const Tiny_State *state,
                                Tiny_Context ctx) {
    thread->ctx = ctx;

    thread->state = state;

    thread->gcHead = NULL;
    thread->numObjects = 0;
    // TODO: Use INIT_GC_THRESH definition
    thread->maxNumObjects = 8;

    thread->globalVars = NULL;

    thread->pc = -1;
    thread->fp = thread->sp = 0;

    thread->retVal = Tiny_Null;

    thread->fc = 0;

    thread->userdata = NULL;
}

void Tiny_InitThread(Tiny_StateThread *thread, const Tiny_State *state) {
    Tiny_InitThreadWithContext(thread, state, Tiny_DefaultContext);
}

static void AllocGlobals(Tiny_StateThread *thread) {
    // If the global variables haven't been allocated yet,
    // do that
    if (!thread->globalVars) {
        thread->globalVars =
            TMalloc(&thread->ctx, sizeof(Tiny_Value) * thread->state->numGlobalVars);
        memset(thread->globalVars, 0, sizeof(Tiny_Value) * thread->state->numGlobalVars);
    }
}

void Tiny_StartThread(Tiny_StateThread *thread) {
    AllocGlobals(thread);

    // TODO: Eventually move to an actual entry point
    thread->pc = 0;
}

inline static bool ExecuteCycle(Tiny_StateThread *thread);

int Tiny_GetGlobalIndex(const Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Tiny_Symbol *sym = state->globalSymbols[i];

        if (sym->type == TINY_SYM_GLOBAL && strcmp(sym->name, name) == 0) {
            return sym->var.index;
        }
    }

    return -1;
}

int Tiny_GetFunctionIndex(const Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Tiny_Symbol *sym = state->globalSymbols[i];

        if (sym->type == TINY_SYM_FUNCTION && strcmp(sym->name, name) == 0) {
            return sym->func.index;
        }
    }

    return -1;
}

static void DoPushIndir(Tiny_StateThread *thread, uint8_t nargs);
static void DoPush(Tiny_StateThread *thread, Tiny_Value value);

Tiny_Value Tiny_GetGlobal(const Tiny_StateThread *thread, int globalIndex) {
    assert(globalIndex >= 0 && globalIndex < thread->state->numGlobalVars);
    assert(thread->globalVars);

    return thread->globalVars[globalIndex];
}

void Tiny_SetGlobal(Tiny_StateThread *thread, int globalIndex, Tiny_Value value) {
    assert(globalIndex >= 0 && globalIndex < thread->state->numGlobalVars);
    assert(thread->globalVars);

    thread->globalVars[globalIndex] = value;
}

Tiny_Value Tiny_CallFunction(Tiny_StateThread *thread, int functionIndex, const Tiny_Value *args,
                             int count) {
    assert(thread->state && functionIndex >= 0);

    int pc, fp, sp, fc;

    pc = thread->pc;
    fp = thread->fp;
    sp = thread->sp;
    fc = thread->fc;

    Tiny_Value retVal = thread->retVal;

    AllocGlobals(thread);

    for (int i = 0; i < count; ++i) {
        DoPush(thread, args[i]);
    }

    thread->pc = thread->state->functionPcs[functionIndex];
    DoPushIndir(thread, count);

    // Keep executing until the indir stack is restored (i.e. function is done)
    while (thread->fc > fc && ExecuteCycle(thread));

    Tiny_Value newRetVal = thread->retVal;

    thread->pc = pc;
    thread->fp = fp;
    thread->sp = sp;
    thread->fc = fc;

    thread->retVal = retVal;

    return newRetVal;
}

bool Tiny_ExecuteCycle(Tiny_StateThread *thread) { return ExecuteCycle(thread); }

void Tiny_Run(Tiny_StateThread *thread) { while (ExecuteCycle(thread)); }

void Tiny_DestroyThread(Tiny_StateThread *thread) {
    thread->pc = -1;

    // Free all objects in the gc list
    while (thread->gcHead) {
        Tiny_Object *next = thread->gcHead->next;
        DeleteObject(&thread->ctx, thread->gcHead);
        thread->gcHead = next;
    }

    // Free all global variables
    TFree(&thread->ctx, thread->globalVars);
}

static void MarkAll(Tiny_StateThread *thread) {
    assert(thread->state);

    Tiny_ProtectFromGC(thread->retVal);

    for (int i = 0; i < thread->sp; ++i) Tiny_ProtectFromGC(thread->stack[i]);

    for (int i = 0; i < thread->state->numGlobalVars; ++i)
        Tiny_ProtectFromGC(thread->globalVars[i]);
}

static void GenerateCode(Tiny_State *state, Word inst) {
    sb_push(&state->ctx, state->program, inst);
}

// Tiny_State* state, const T* value, int* pPos
//
// Pads the bytecode until we are aligned for T, then writes out the bytes for T,
// storing starting pos in pPos.
#define GEN_VALUE(state, pValue, pPos)                                   \
    do {                                                                 \
        size_t align_minus_one = alignof(*pValue) - 1;                   \
        int count = sb_count(state->program);                            \
        int padded_count = (count + align_minus_one) & ~align_minus_one; \
        for (int i = 0; i < padded_count - count; ++i) {                 \
            GenerateCode(state, TINY_OP_MISALIGNED_INSTRUCTION);         \
        }                                                                \
        *pPos = sb_count(state->program);                                \
        Word *wp = (Word *)pValue;                                       \
        for (int i = 0; i < sizeof(*pValue); ++i) {                      \
            GenerateCode(state, *wp++);                                  \
        }                                                                \
    } while (0)

// Same as GEN_VALUE but ignore position
#define GEN_VALUE_NOPOS(state, pValue)  \
    do {                                \
        int pos = 0;                    \
        GEN_VALUE(state, pValue, &pos); \
    } while (0)

// Tiny_State* state, const T* pValue, int pc
//
// Writes out the value stored in pValue to bytecode stream at pc
// asserting that pc is aligned for the value before doing so
#define GEN_VALUE_AT(state, pValue, pc)             \
    do {                                            \
        assert(pc % alignof(*pValue) == 0);         \
        Word *wp = (Word *)pValue;                  \
        for (int i = 0; i < sizeof(*pValue); ++i) { \
            state->program[pc + i] = *wp++;         \
        }                                           \
    } while (0)

static int RegisterString(Tiny_State *state, const char *string) {
    for (int i = 0; i < state->numStrings; ++i) {
        if (strcmp(state->strings[i], string) == 0) return i;
    }

    assert(state->numStrings < MAX_STRINGS);
    state->strings[state->numStrings++] = CloneString(&state->ctx, string);

    return state->numStrings - 1;
}

static Tiny_Symbol *GetPrimTag(Tiny_SymbolType type) {
    static Tiny_Symbol prims[] = {{
                                      TINY_SYM_TAG_VOID,
                                      (char *)"void",
                                  },
                                  {
                                      TINY_SYM_TAG_BOOL,
                                      (char *)"bool",
                                  },
                                  {TINY_SYM_TAG_INT, (char *)"int"},
                                  {TINY_SYM_TAG_FLOAT, (char *)"float"},
                                  {TINY_SYM_TAG_STR, (char *)"str"},
                                  {TINY_SYM_TAG_ANY, (char *)"any"}};

    return &prims[type - TINY_SYM_TAG_VOID];
}

static Tiny_Symbol *Symbol_create(Tiny_SymbolType type, const char *name, Tiny_State *state) {
    Tiny_Symbol *sym = TMalloc(&state->ctx, sizeof(Tiny_Symbol));

    sym->name = CloneString(&state->ctx, name);
    sym->type = type;
    sym->pos = state->l.pos;

    return sym;
}

static void Symbol_destroy(Tiny_Symbol *sym, Tiny_Context *ctx) {
    if (sym->type == TINY_SYM_FUNCTION) {
        for (int i = 0; i < sb_count(sym->func.args); ++i) {
            Tiny_Symbol *arg = sym->func.args[i];

            assert(arg->type == TINY_SYM_LOCAL);

            Symbol_destroy(arg, ctx);
        }

        sb_free(ctx, sym->func.args);

        for (int i = 0; i < sb_count(sym->func.locals); ++i) {
            Tiny_Symbol *local = sym->func.locals[i];

            assert(local->type == TINY_SYM_LOCAL);

            Symbol_destroy(local, ctx);
        }

        sb_free(ctx, sym->func.locals);
    } else if (sym->type == TINY_SYM_FOREIGN_FUNCTION) {
        sb_free(ctx, sym->foreignFunc.argTags);
    } else if (sym->type == TINY_SYM_TAG_STRUCT) {
        for (int i = 0; i < sb_count(sym->sstruct.fields); ++i) {
            Symbol_destroy(sym->sstruct.fields[i], ctx);
        }

        sb_free(ctx, sym->sstruct.fields);
    }

    TFree(ctx, sym->name);
    TFree(ctx, sym);
}

static void OpenScope(Tiny_State *state) { ++state->currScope; }

static void CloseScope(Tiny_State *state) {
    if (state->currFunc) {
        for (int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
            Tiny_Symbol *sym = state->currFunc->func.locals[i];

            assert(sym->type == TINY_SYM_LOCAL);

            if (sym->var.scope == state->currScope) {
                sym->var.scopeEnded = true;
            }
        }
    }

    --state->currScope;
}

#define ST_MASK(symType) (1UL << (symType))

#define ST_MASK_FUNC (ST_MASK(TINY_SYM_FUNCTION) | ST_MASK(TINY_SYM_FOREIGN_FUNCTION))
#define ST_MASK_VAR (ST_MASK(TINY_SYM_LOCAL) | ST_MASK(TINY_SYM_GLOBAL))
#define ST_MASK_VAR_OR_CONST ST_MASK_VAR | ST_MASK(TINY_SYM_CONST)

static Tiny_Symbol *FindSymbol(Tiny_State *state, const char *name, uint32_t mask) {
    // This clever trick let's me unify the codepaths for symbols
    // that can be named and can't be named
    if (name == ANON_SYM_NAME) {
        return NULL;
    }

    if (state->currFunc && ST_MASK(TINY_SYM_LOCAL) & mask) {
        // Check local variables
        for (int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
            Tiny_Symbol *sym = state->currFunc->func.locals[i];

            assert(sym->type == TINY_SYM_LOCAL);

            // Make sure that it's available in the current scope too
            if (!sym->var.scopeEnded && strcmp(sym->name, name) == 0) {
                return sym;
            }
        }

        // Check arguments
        for (int i = 0; i < sb_count(state->currFunc->func.args); ++i) {
            Tiny_Symbol *sym = state->currFunc->func.args[i];

            assert(sym->type == TINY_SYM_LOCAL);

            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
    }

    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Tiny_Symbol *sym = state->globalSymbols[i];

        if (ST_MASK(sym->type) & mask) {
            if (strcmp(sym->name, name) == 0) return sym;
        }
    }

    return NULL;
}

static Tiny_Symbol *DeclareGlobalVar(Tiny_State *state, const char *name) {
    Tiny_Symbol *sym = FindSymbol(state, name, ST_MASK(TINY_SYM_GLOBAL) | ST_MASK(TINY_SYM_CONST));

    if (sym) {
        assert(sym->type == TINY_SYM_GLOBAL || sym->type == TINY_SYM_CONST);

        ReportErrorSL(state,
                      "Attempted to declare multiple global entities with the same "
                      "name '%s'.",
                      name);
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_GLOBAL, name, state);

    newNode->var.initialized = false;
    newNode->var.index = state->numGlobalVars;
    newNode->var.scope = 0;  // Global variable scope don't matter
    newNode->var.scopeEnded = false;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    state->numGlobalVars += 1;

    return newNode;
}

// This expects nargs to be known beforehand because arguments are
// evaluated/pushed left-to-right so the first argument is actually at -nargs
// position relative to frame pointer We could reverse it, but this works out
// nicely for Foreign calls since we can just supply a pointer to the initial
// arg instead of reversing them.
static Tiny_Symbol *DeclareArgument(Tiny_State *state, const char *name, Tiny_Symbol *tag,
                                    int nargs) {
    assert(state->currFunc);
    assert(tag);

    // At this time there would be no locals declared so this should be fine.
    // Regardless, you shan't have locals + args have the same name.
    Tiny_Symbol *prevSym = FindSymbol(state, name, ST_MASK(TINY_SYM_LOCAL));
    if (prevSym) {
        assert(prevSym->type == TINY_SYM_LOCAL);

        ReportErrorSL(state, "Function '%s' has multiple arguments with the name '%s'.\n",
                      state->currFunc->name, name);
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = -nargs + sb_count(state->currFunc->func.args);
    newNode->var.scope = 0;  // These should be accessible anywhere in the function
    newNode->var.tag = tag;

    sb_push(&state->ctx, state->currFunc->func.args, newNode);

    return newNode;
}

static Tiny_Symbol *DeclareLocal(Tiny_State *state, const char *name) {
    assert(state->currFunc);

    Tiny_Symbol *prevSym = FindSymbol(state, name, ST_MASK(TINY_SYM_LOCAL));
    if (prevSym) {
        assert(prevSym->type == TINY_SYM_LOCAL);

        ReportErrorSL(state,
                      "Function '%s' has multiple locals in the same scope with "
                      "name '%s'.\n",
                      state->currFunc->name, name);
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = sb_count(state->currFunc->func.locals);
    newNode->var.scope = state->currScope;

    sb_push(&state->ctx, state->currFunc->func.locals, newNode);

    return newNode;
}

// Declares a local or global depending on whether we are inside a func
static Tiny_Symbol *DeclareVar(Tiny_State *state, const char *name) {
    return state->currFunc ? DeclareLocal(state, name) : DeclareGlobalVar(state, name);
}

static Tiny_Symbol *DeclareConst(Tiny_State *state, const char *name, Tiny_Symbol *tag) {
    Tiny_Symbol *sym = FindSymbol(state, name, ST_MASK_VAR_OR_CONST);

    if (sym) {
        assert(sym->type == TINY_SYM_CONST || sym->type == TINY_SYM_LOCAL ||
               sym->type == TINY_SYM_GLOBAL);

        ReportErrorSL(state,
                      "Attempted to define constant with the same name '%s' as "
                      "another value.\n",
                      name);
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_CONST, name, state);

    newNode->constant.tag = tag;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    return newNode;
}

static Tiny_Symbol *DeclareFunction(Tiny_State *state, const char *name) {
    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_FUNCTION, name, state);

    newNode->func.index = state->numFunctions;
    newNode->func.args = NULL;
    newNode->func.locals = NULL;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    state->numFunctions += 1;

    return newNode;
}

static Tiny_BindFunctionResultType BindFunction(Tiny_State *state, const char *name,
                                                Tiny_Symbol **argTags, bool varargs,
                                                Tiny_Symbol *returnTag, Tiny_ForeignFunction func) {
    Tiny_Symbol *prevSym = FindSymbol(state, name, ST_MASK(TINY_SYM_FOREIGN_FUNCTION));
    if (prevSym) {
        assert(prevSym->type == TINY_SYM_FOREIGN_FUNCTION);
        return TINY_BIND_FUNCTION_ERROR_DUPLICATE;
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_FOREIGN_FUNCTION, name, state);

    newNode->foreignFunc.index = state->numForeignFunctions;

    newNode->foreignFunc.argTags = argTags;
    newNode->foreignFunc.varargs = varargs;

    newNode->foreignFunc.returnTag = returnTag;

    newNode->foreignFunc.callee = func;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    state->numForeignFunctions += 1;

    return TINY_BIND_FUNCTION_SUCCESS;
}

static Tiny_Symbol *GetTagFromName(Tiny_State *state, const char *name, bool declareStruct);

void Tiny_RegisterType(Tiny_State *state, const char *name) {
    Tiny_Symbol *s = GetTagFromName(state, name, false);

    if (s) {
        if (s->type == TINY_SYM_TAG_STRUCT && !s->sstruct.defined) {
            // If there's a struct that's undefined with the same name, the Tiny code is probably
            // referring to this type that we're about to define here, so just update it in place
            // to be an opaque foreign type.
            s->type = TINY_SYM_TAG_FOREIGN;
        }

        return;
    }

    s = Symbol_create(TINY_SYM_TAG_FOREIGN, name, state);

    sb_push(&state->ctx, state->globalSymbols, s);
}

static Tiny_Symbol *ParseTypeL(Tiny_State *state, Tiny_Lexer *l);

Tiny_BindFunctionResultType Tiny_BindFunction(Tiny_State *state, const char *sig,
                                              Tiny_ForeignFunction func) {
    Tiny_Lexer l;

    Tiny_InitLexer(&l, "(bind signature)", sig, state->ctx);

    Tiny_GetToken(&l);

    assert(l.lastTok == TINY_TOK_IDENT);

    char *name = CloneString(&state->ctx, l.lexeme);

    Tiny_GetToken(&l);

    if (l.lastTok != TINY_TOK_OPENPAREN) {
        // Just the name
        Tiny_BindFunctionResultType res =
            BindFunction(state, name, NULL, true, GetPrimTag(TINY_SYM_TAG_ANY), func);
        Tiny_DestroyLexer(&l);

        TFree(&state->ctx, name);

        return res;
    }

    Tiny_GetToken(&l);

    Tiny_Symbol **argTags = NULL;
    bool varargs = false;

    for (;;) {
        if (l.lastTok == TINY_TOK_CLOSEPAREN) {
            break;
        }

        if (l.lastTok == TINY_TOK_COMMA) {
            Tiny_GetToken(&l);
        } else if (l.lastTok == TINY_TOK_ELLIPSIS) {
            varargs = true;
            Tiny_GetToken(&l);
        } else {
            Tiny_Symbol *s = ParseTypeL(state, &l);

            assert(s);

            sb_push(&state->ctx, argTags, s);
        }
    }

    assert(l.lastTok == TINY_TOK_CLOSEPAREN);

    Tiny_GetToken(&l);

    Tiny_Symbol *returnTag = GetPrimTag(TINY_SYM_TAG_ANY);

    if (l.lastTok == TINY_TOK_COLON) {
        Tiny_GetToken(&l);

        returnTag = ParseTypeL(state, &l);
        assert(returnTag);
    }

    Tiny_BindFunctionResultType res = BindFunction(state, name, argTags, varargs, returnTag, func);

    Tiny_DestroyLexer(&l);

    TFree(&state->ctx, name);

    return res;
}

void Tiny_BindConstBool(Tiny_State *state, const char *name, bool b) {
    DeclareConst(state, name, GetPrimTag(TINY_SYM_TAG_BOOL))->constant.bValue = b;
}

void Tiny_BindConstInt(Tiny_State *state, const char *name, Tiny_Int i) {
    DeclareConst(state, name, GetPrimTag(TINY_SYM_TAG_INT))->constant.iValue = i;
}

void Tiny_BindConstFloat(Tiny_State *state, const char *name, Tiny_Float f) {
    DeclareConst(state, name, GetPrimTag(TINY_SYM_TAG_FLOAT))->constant.fValue = f;
}

void Tiny_BindConstString(Tiny_State *state, const char *name, const char *string) {
    DeclareConst(state, name, GetPrimTag(TINY_SYM_TAG_STR))->constant.sIndex =
        RegisterString(state, string);
}

// const Tiny_State* state, int* pPC, T* pDest
//
// Aligns pPC for T and then reads T
#define READ_VALUE_AT(state, pPC, pDest)                      \
    do {                                                      \
        size_t align_minus_one = alignof(*pDest) - 1;         \
        *pPC = (*pPC + align_minus_one) & ~align_minus_one;   \
        memcpy(pDest, state->program + *pPC, sizeof(*pDest)); \
        *pPC += sizeof(*pDest) / sizeof(Word);                \
    } while (0)

static Tiny_Int ReadInteger(Tiny_StateThread *thread) {
    Tiny_Int val = 0;

    READ_VALUE_AT(thread->state, &thread->pc, &val);

    return val;
}

static Tiny_ConstantIndex ReadConstIndex(Tiny_StateThread *thread) {
    Tiny_ConstantIndex val = 0;

    READ_VALUE_AT(thread->state, &thread->pc, &val);

    return val;
}

static Tiny_Float ReadFloat(Tiny_StateThread *thread) {
    Tiny_Float val = 0;

    READ_VALUE_AT(thread->state, &thread->pc, &val);

    return val;
}

static void DoPush(Tiny_StateThread *thread, Tiny_Value value) {
    assert(thread->sp < TINY_THREAD_STACK_SIZE);

    thread->stack[thread->sp++] = value;
}

static inline Tiny_Value DoPop(Tiny_StateThread *thread) { return thread->stack[--thread->sp]; }

static void DoPushIndir(Tiny_StateThread *thread, uint8_t nargs) {
    assert(thread->fc < TINY_THREAD_MAX_CALL_DEPTH);

    thread->frames[thread->fc++] = (Tiny_Frame){thread->pc, thread->fp, nargs};
    thread->fp = thread->sp;
}

static void DoPopIndir(Tiny_StateThread *thread) {
    assert(thread->fc > 0);

    thread->sp = thread->fp;

    Tiny_Frame frame = thread->frames[--thread->fc];

    thread->sp -= frame.nargs;
    thread->fp = frame.fp;
    thread->pc = frame.pc;
}

inline static bool ExpectBool(const Tiny_Value value) {
    assert(value.type == TINY_VAL_BOOL);
    return value.boolean;
}

inline static bool ExecuteCycle(Tiny_StateThread *thread) {
    assert(thread && thread->state);

    if (thread->pc < 0) return false;

    const Tiny_State *state = thread->state;

    switch (state->program[thread->pc]) {
        case TINY_OP_PUSH_NULL: {
            ++thread->pc;
            DoPush(thread, Tiny_Null);
        } break;

        case TINY_OP_PUSH_NULL_N: {
            ++thread->pc;
            Word n = state->program[thread->pc++];
            memset(&thread->stack[thread->sp], 0, sizeof(Tiny_Value) * n);
            thread->sp += n;
        } break;

        case TINY_OP_PUSH_TRUE: {
            ++thread->pc;
            DoPush(thread, Tiny_NewBool(true));
        } break;

        case TINY_OP_PUSH_FALSE: {
            ++thread->pc;
            DoPush(thread, Tiny_NewBool(false));
        } break;

        case TINY_OP_PUSH_INT: {
            ++thread->pc;
            DoPush(thread, Tiny_NewInt(ReadInteger(thread)));
        } break;

        case TINY_OP_PUSH_0: {
            ++thread->pc;
            DoPush(thread, Tiny_NewInt(0));
        } break;

        case TINY_OP_PUSH_1: {
            ++thread->pc;
            DoPush(thread, Tiny_NewInt(1));
        } break;

        case TINY_OP_PUSH_CHAR: {
            ++thread->pc;
            DoPush(thread, Tiny_NewInt(state->program[thread->pc++]));
        } break;

        case TINY_OP_PUSH_FLOAT: {
            ++thread->pc;
            DoPush(thread, Tiny_NewFloat(ReadFloat(thread)));
        } break;

        case TINY_OP_PUSH_STRING: {
            ++thread->pc;
            Tiny_Int stringIndex = ReadInteger(thread);
            DoPush(thread, Tiny_NewConstString(state->strings[stringIndex]));
        } break;

        case TINY_OP_PUSH_STRING_FF: {
            ++thread->pc;
            Word sIndex = state->program[thread->pc++];
            DoPush(thread, Tiny_NewConstString(state->strings[sIndex]));
        } break;

        case TINY_OP_PUSH_STRUCT: {
            ++thread->pc;
            Word nFields = state->program[thread->pc++];
            assert(nFields > 0);

            Tiny_Object *obj = NewStructObject(thread, nFields);
            memcpy(obj->ostruct.fields, &thread->stack[thread->sp - nFields],
                   sizeof(Tiny_Value) * nFields);
            thread->sp -= nFields;

            DoPush(thread, (Tiny_Value){.type = TINY_VAL_STRUCT, .obj = obj});
        } break;

        case TINY_OP_STRUCT_GET: {
            ++thread->pc;
            Word i = state->program[thread->pc++];
            Tiny_Value *vstruct = &thread->stack[thread->sp - 1];

            assert(vstruct->type == TINY_VAL_STRUCT);
            assert(i >= 0 && i < vstruct->obj->ostruct.n);

            *vstruct = vstruct->obj->ostruct.fields[i];
        } break;

        case TINY_OP_STRUCT_SET: {
            ++thread->pc;
            Word i = state->program[thread->pc++];

            Tiny_Value vstruct = DoPop(thread);
            Tiny_Value val = DoPop(thread);

            assert(vstruct.type == TINY_VAL_STRUCT);
            assert(i >= 0 && i < vstruct.obj->ostruct.n);

            vstruct.obj->ostruct.fields[i] = val;
        } break;

#define BIN_OP(OP, operator)                                      \
    case TINY_OP_##OP: {                                          \
        Tiny_Value *a = &thread->stack[thread->sp - 2];           \
        Tiny_Value b = thread->stack[--thread->sp];               \
        if (a->type == TINY_VAL_INT && b.type == TINY_VAL_INT) {  \
            a->i = a->i operator b.i;                             \
        } else {                                                  \
            if (a->type == TINY_VAL_INT) a->f = (Tiny_Float)a->i; \
            if (b.type == TINY_VAL_INT) b.f = (Tiny_Float)b.i;    \
            a->type = TINY_VAL_FLOAT;                             \
            a->f = a->f operator b.f;                             \
        }                                                         \
        ++thread->pc;                                             \
    } break;

#define BIN_OP_INT(OP, operator)                             \
    case TINY_OP_##OP: {                                     \
        Tiny_Value val2 = DoPop(thread);                     \
        Tiny_Value val1 = DoPop(thread);                     \
        DoPush(thread, Tiny_NewInt(val1.i operator val2.i)); \
        ++thread->pc;                                        \
    } break;

#define REL_OP(OP, operator)                                                     \
    case TINY_OP_##OP: {                                                         \
        Tiny_Value *a = &thread->stack[thread->sp - 2];                          \
        Tiny_Value b = thread->stack[--thread->sp];                              \
        bool result;                                                             \
        if (a->type == TINY_VAL_FLOAT || b.type == TINY_VAL_FLOAT) {             \
            Tiny_Float af = (a->type == TINY_VAL_INT) ? (Tiny_Float)a->i : a->f; \
            Tiny_Float bf = (b.type == TINY_VAL_INT) ? (Tiny_Float)b.i : b.f;    \
            result = af operator bf;                                             \
        } else {                                                                 \
            result = a->i operator b.i;                                          \
        }                                                                        \
        a->type = TINY_VAL_BOOL;                                                 \
        a->boolean = result;                                                     \
        ++thread->pc;                                                            \
    } break;

            BIN_OP(ADD, +)
            BIN_OP(SUB, -)
            BIN_OP(MUL, *)
            BIN_OP(DIV, /)
            BIN_OP_INT(MOD, %)
            BIN_OP_INT(OR, |)
            BIN_OP_INT(AND, &)
            BIN_OP_INT(SHIFT_LEFT, <<)
            BIN_OP_INT(SHIFT_RIGHT, >>)

            REL_OP(LT, <)
            REL_OP(GT, >)
            REL_OP(GTE, >=)
            REL_OP(LTE, <=)

#undef BIN_OP
#undef BIN_OP_INT
#undef REL_OP

        case TINY_OP_ADD1: {
            ++thread->pc;
            thread->stack[thread->sp - 1].i += 1;
        } break;

        case TINY_OP_SUB1: {
            ++thread->pc;
            thread->stack[thread->sp - 1].i -= 1;
        } break;

        case TINY_OP_EQU: {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);
            DoPush(thread, Tiny_NewBool(Tiny_AreValuesEqual(a, b)));
        } break;

        case TINY_OP_LOG_NOT: {
            ++thread->pc;
            Tiny_Value a = DoPop(thread);
            DoPush(thread, Tiny_NewBool(!ExpectBool(a)));
        } break;

        case TINY_OP_SET: {
            ++thread->pc;
            Tiny_ConstantIndex varIdx = ReadConstIndex(thread);
            thread->globalVars[varIdx] = DoPop(thread);
        } break;

        case TINY_OP_GET: {
            ++thread->pc;
            Tiny_ConstantIndex varIdx = ReadConstIndex(thread);
            DoPush(thread, thread->globalVars[varIdx]);
        } break;

        case TINY_OP_GOTO: {
            ++thread->pc;
            Tiny_ConstantIndex newPc = ReadConstIndex(thread);
            thread->pc = newPc;
        } break;

        case TINY_OP_GOTOZ: {
            ++thread->pc;
            Tiny_ConstantIndex newPc = ReadConstIndex(thread);
            Tiny_Value val = DoPop(thread);
            if (!ExpectBool(val)) thread->pc = newPc;
        } break;

        case TINY_OP_CALL: {
            ++thread->pc;
            Word nargs = state->program[thread->pc++];
            Tiny_ConstantIndex funcIdx = ReadConstIndex(thread);
            DoPushIndir(thread, nargs);
            thread->pc = state->functionPcs[funcIdx];
        } break;

        case TINY_OP_RETURN: {
            thread->retVal = Tiny_Null;
            DoPopIndir(thread);
        } break;

        case TINY_OP_RETURN_VALUE: {
            thread->retVal = DoPop(thread);
            DoPopIndir(thread);
        } break;

        case TINY_OP_CALLF: {
            ++thread->pc;
            Word nargs = state->program[thread->pc++];
            Tiny_ConstantIndex fIdx = ReadConstIndex(thread);
            int prevSize = thread->sp - nargs;
            thread->retVal = state->foreignFunctions[fIdx](thread, &thread->stack[prevSize], nargs);
            thread->sp = prevSize;
        } break;

        case TINY_OP_GETLOCAL: {
            ++thread->pc;
            Tiny_ConstantIndex localIdx = ReadConstIndex(thread);
            DoPush(thread, thread->stack[thread->fp + localIdx]);
        } break;

        case TINY_OP_GETLOCAL_W: {
            ++thread->pc;
            Word localIdx = state->program[thread->pc++];
            DoPush(thread, thread->stack[thread->fp + localIdx]);
        } break;

        case TINY_OP_SETLOCAL: {
            ++thread->pc;
            Tiny_ConstantIndex localIdx = ReadConstIndex(thread);
            thread->stack[thread->fp + localIdx] = DoPop(thread);
        } break;

        case TINY_OP_GET_RETVAL: {
            ++thread->pc;
            DoPush(thread, thread->retVal);
        } break;

        case TINY_OP_HALT: {
            thread->pc = -1;
        } break;

        case TINY_OP_MISALIGNED_INSTRUCTION: {
            assert(false && "Misaligned instruction encountered");
        } break;

        default: {
            assert(false && "Unknown opcode encountered");
        } break;
    }

    if (thread->numObjects >= thread->maxNumObjects) {
        GarbageCollect(thread);
    }

    return true;
}

static Tiny_Expr *Expr_create(Tiny_ExprType type, Tiny_State *state) {
    Tiny_Expr *exp = Tiny_ArenaAlloc(&state->parserArena, sizeof(Tiny_Expr), sizeof(void *));

    exp->next = NULL;
    exp->pos = state->l.pos;
    exp->lineNumber = state->l.lineNumber;
    exp->type = type;

    exp->tag = NULL;

    return exp;
}

static Tiny_Expr *ParseExpr(Tiny_State *state);

static void GetExpectTokenSL(Tiny_State *state, Tiny_TokenKind tok, const char *msg) {
    Tiny_GetToken(&state->l);
    if (state->l.lastTok != tok) {
        ReportErrorSL(state, msg);
    }
}

static void ReportErrorE(Tiny_State *state, const Tiny_Expr *exp, const char *s, ...) {
    va_list args;
    va_start(args, s);

    state->compileErrorResult.type = TINY_COMPILE_ERROR;

    Tiny_FormatErrorV(state->compileErrorResult.error.msg,
                      sizeof(state->compileErrorResult.error.msg), state->l.fileName, state->l.src,
                      exp->pos, s, args);

    va_end(args);

    longjmp(state->compileErrorJmpBufs[state->compileCallNestCount - 1], 1);
}

static void ReportErrorS(Tiny_State *state, const Tiny_Symbol *sym, const char *s, ...) {
    va_list args;
    va_start(args, s);

    state->compileErrorResult.type = TINY_COMPILE_ERROR;

    Tiny_FormatErrorV(state->compileErrorResult.error.msg,
                      sizeof(state->compileErrorResult.error.msg), state->l.fileName, state->l.src,
                      sym->pos, s, args);

    va_end(args);

    longjmp(state->compileErrorJmpBufs[state->compileCallNestCount - 1], 1);
}

static void ExpectTokenL(Tiny_State *state, Tiny_Lexer *l, Tiny_TokenKind tok, const char *msg) {
    if (l->lastTok != tok) {
        ReportErrorL(state, l, msg);
    }
}

static void ExpectTokenSL(Tiny_State *state, Tiny_TokenKind tok, const char *msg) {
    ExpectTokenL(state, &state->l, tok, msg);
}

static Tiny_Symbol *DeclareStruct(Tiny_State *state, const char *name, bool search) {
    if (search) {
        Tiny_Symbol *s = FindSymbol(state, name, ST_MASK(TINY_SYM_TAG_STRUCT));
        if (s) return s;
    }

    Tiny_Symbol *s = Symbol_create(TINY_SYM_TAG_STRUCT, name, state);

    s->sstruct.defined = false;
    s->sstruct.fields = NULL;

    sb_push(&state->ctx, state->globalSymbols, s);

    return s;
}

static Tiny_Symbol *GetTagFromName(Tiny_State *state, const char *name, bool declareStruct) {
    if (strcmp(name, "void") == 0)
        return GetPrimTag(TINY_SYM_TAG_VOID);
    else if (strcmp(name, "bool") == 0)
        return GetPrimTag(TINY_SYM_TAG_BOOL);
    else if (strcmp(name, "int") == 0)
        return GetPrimTag(TINY_SYM_TAG_INT);
    else if (strcmp(name, "float") == 0)
        return GetPrimTag(TINY_SYM_TAG_FLOAT);
    else if (strcmp(name, "str") == 0)
        return GetPrimTag(TINY_SYM_TAG_STR);
    else if (strcmp(name, "any") == 0)
        return GetPrimTag(TINY_SYM_TAG_ANY);
    else {
        for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
            Tiny_Symbol *s = state->globalSymbols[i];

            if ((s->type == TINY_SYM_TAG_FOREIGN || s->type == TINY_SYM_TAG_STRUCT) &&
                strcmp(s->name, name) == 0) {
                return s;
            }
        }

        if (declareStruct) {
            return DeclareStruct(state, name, false);
        }
    }

    return NULL;
}

static const char *GetTagName(const Tiny_Symbol *tag);

static Tiny_Symbol *GetFieldTag(Tiny_Symbol *s, const char *name, int *index) {
    assert(s->type == TINY_SYM_TAG_STRUCT);
    assert(s->sstruct.defined);

    for (int i = 0; i < sb_count(s->sstruct.fields); ++i) {
        Tiny_Symbol *f = s->sstruct.fields[i];

        if (strcmp(f->name, name) == 0) {
            if (index) *index = i;
            return f->fieldTag;
        }
    }

    if (index) *index = -1;

    return NULL;
}

static Tiny_TokenKind GetNextToken(Tiny_State *state) { return Tiny_GetToken(&state->l); }

static Tiny_Symbol *ParseTypeL(Tiny_State *state, Tiny_Lexer *l) {
    ExpectTokenL(state, l, TINY_TOK_IDENT, "Expected identifier for typename.");

    Tiny_Symbol *s = GetTagFromName(state, l->lexeme, true);

    if (!s) {
        ReportErrorL(state, l, "%s doesn't name a type.", l->lexeme);
    }

    Tiny_GetToken(l);

    return s;
}

static Tiny_Symbol *ParseTypeSL(Tiny_State *state) { return ParseTypeL(state, &state->l); }

static Tiny_Expr *ParseStatement(Tiny_State *state);

static Tiny_Expr *ParseIf(Tiny_State *state) {
    Tiny_Expr *exp = Expr_create(TINY_EXP_IF, state);

    GetNextToken(state);

    exp->ifx.cond = ParseExpr(state);
    exp->ifx.body = ParseStatement(state);

    if (state->l.lastTok == TINY_TOK_ELSE) {
        GetNextToken(state);
        exp->ifx.alt = ParseStatement(state);
    } else
        exp->ifx.alt = NULL;

    return exp;
}

static Tiny_Expr *ParseBlock(Tiny_State *state) {
    assert(state->l.lastTok == TINY_TOK_OPENCURLY);

    Tiny_Expr *exp = Expr_create(TINY_EXP_BLOCK, state);

    exp->blockHead = NULL;
    Tiny_Expr *blockTail = NULL;

    GetNextToken(state);

    OpenScope(state);

    while (state->l.lastTok != TINY_TOK_CLOSECURLY) {
        Tiny_Expr *e = ParseStatement(state);
        assert(e);

        TINY_LL_APPEND(exp->blockHead, blockTail, e);
    }

    GetNextToken(state);

    CloseScope(state);

    return exp;
}

static Tiny_Expr *ParseFunc(Tiny_State *state) {
    assert(state->l.lastTok == TINY_TOK_FUNC);

    if (state->currFunc) {
        ReportErrorSL(state, "Attempted to define function inside of function '%s'.",
                      state->currFunc->name);
    }

    Tiny_Expr *exp = Expr_create(TINY_EXP_PROC, state);

    GetExpectTokenSL(state, TINY_TOK_IDENT, "Function name must be identifier!");

    exp->proc.decl = DeclareFunction(state, state->l.lexeme);
    state->currFunc = exp->proc.decl;

    GetExpectTokenSL(state, TINY_TOK_OPENPAREN, "Expected '(' after function name");

    GetNextToken(state);

    typedef struct {
        char *name;
        Tiny_Symbol *tag;
    } Arg;

    Arg *args = NULL;  // array

    while (state->l.lastTok != TINY_TOK_CLOSEPAREN) {
        ExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier in function parameter list");

        Arg arg;

        arg.name = CloneString(&state->ctx, state->l.lexeme);
        GetNextToken(state);

        if (state->l.lastTok != TINY_TOK_COLON) {
            ReportErrorSL(state, "Expected ':' after %s", arg.name);
        }

        GetNextToken(state);

        arg.tag = ParseTypeSL(state);

        sb_push(&state->ctx, args, arg);

        if (state->l.lastTok != TINY_TOK_CLOSEPAREN && state->l.lastTok != TINY_TOK_COMMA) {
            ReportErrorSL(state,
                          "Expected ')' or ',' after parameter name in function "
                          "parameter list.");
        }

        if (state->l.lastTok == TINY_TOK_COMMA) GetNextToken(state);
    }

    for (int i = 0; i < sb_count(args); ++i) {
        DeclareArgument(state, args[i].name, args[i].tag, sb_count(args));
        TFree(&state->ctx, args[i].name);
    }

    sb_free(&state->ctx, args);

    GetNextToken(state);

    if (state->l.lastTok != TINY_TOK_COLON) {
        exp->proc.decl->func.returnTag = GetPrimTag(TINY_SYM_TAG_VOID);
    } else {
        GetNextToken(state);
        exp->proc.decl->func.returnTag = ParseTypeSL(state);
    }

    OpenScope(state);

    exp->proc.body = ParseStatement(state);

    CloseScope(state);

    state->currFunc = NULL;

    return exp;
}

static Tiny_Symbol *ParseStruct(Tiny_State *state) {
    if (state->currFunc) {
        ReportErrorSL(state, "Attempted to declare struct inside func %s. Can't do that bruh.",
                      state->currFunc->name);
    }

    Tiny_TokenPos pos = state->l.pos;

    GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after 'struct'.");

    Tiny_Symbol *s = DeclareStruct(state, state->l.lexeme, true);

    if (s->sstruct.defined) {
        ReportErrorSL(state, "Attempted to define struct %s multiple times.", state->l.lexeme);
    }

    s->pos = pos;
    s->sstruct.defined = true;

    GetExpectTokenSL(state, TINY_TOK_OPENCURLY, "Expected '{' after struct name.");

    GetNextToken(state);

    while (state->l.lastTok != TINY_TOK_CLOSECURLY) {
        ExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier in struct fields.");

        int count = sb_count(s->sstruct.fields);

        if (count >= UCHAR_MAX) {
            ReportErrorSL(state, "Too many fields in struct.");
        }

        for (int i = 0; i < count; ++i) {
            if (strcmp(s->sstruct.fields[i]->name, state->l.lexeme) == 0) {
                ReportErrorSL(state, "Declared multiple fields with the same name %s.",
                              state->l.lexeme);
            }
        }

        Tiny_Symbol *f = Symbol_create(TINY_SYM_FIELD, state->l.lexeme, state);

        GetExpectTokenSL(state, TINY_TOK_COLON, "Expected ':' after field name.");

        GetNextToken(state);

        f->fieldTag = ParseTypeSL(state);

        sb_push(&state->ctx, s->sstruct.fields, f);
    }

    GetNextToken(state);

    if (!s->sstruct.fields) {
        ReportErrorSL(state, "Struct must have at least one field.\n");
    }

    return s;
}

static Tiny_Expr *ParseCall(Tiny_State *state, Tiny_StringNode *ident) {
    assert(state->l.lastTok == TINY_TOK_OPENPAREN);

    Tiny_Expr *exp = Expr_create(TINY_EXP_CALL, state);

    exp->call.argsHead = NULL;
    Tiny_Expr *argsTail = NULL;

    GetNextToken(state);

    while (state->l.lastTok != TINY_TOK_CLOSEPAREN) {
        TINY_LL_APPEND(exp->call.argsHead, argsTail, ParseExpr(state));

        if (state->l.lastTok == TINY_TOK_COMMA) {
            GetNextToken(state);
        } else if (state->l.lastTok != TINY_TOK_CLOSEPAREN) {
            ReportErrorSL(state, "Expected ')' after call.");
        }
    }

    exp->call.calleeName = ident;

    GetNextToken(state);
    return exp;
}

static Tiny_Expr *ParseFactor(Tiny_State *state);

static Tiny_Expr *ParseValue(Tiny_State *state) {
    switch (state->l.lastTok) {
        case TINY_TOK_NULL: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_NULL, state);

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_BOOL: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_BOOL, state);

            exp->boolean = state->l.bValue;

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_IDENT: {
            Tiny_TokenPos pos = state->l.pos;
            int lineNumber = state->l.lineNumber;

            Tiny_StringNode *ident = CreateExprStringNode(state, state->l.lexeme);
            GetNextToken(state);

            if (state->l.lastTok == TINY_TOK_OPENPAREN) {
                return ParseCall(state, ident);
            }

            Tiny_Expr *exp = Expr_create(TINY_EXP_ID, state);

            exp->pos = pos;
            exp->lineNumber = lineNumber;

            exp->id.sym = FindSymbol(state, ident->value, ST_MASK_VAR_OR_CONST);
            exp->id.name = ident;

            return exp;
        } break;

        case TINY_TOK_MINUS:
        case TINY_TOK_BANG: {
            int op = state->l.lastTok;
            GetNextToken(state);
            Tiny_Expr *exp = Expr_create(TINY_EXP_UNARY, state);
            exp->unary.op = op;
            exp->unary.exp = ParseFactor(state);

            return exp;
        } break;

        case TINY_TOK_CHAR: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_CHAR, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_INT: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_INT, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_FLOAT: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_FLOAT, state);
            exp->fValue = state->l.fValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_STRING: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_STRING, state);
            exp->sIndex = RegisterString(state, state->l.lexeme);
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_OPENPAREN: {
            GetNextToken(state);
            Tiny_Expr *inner = ParseExpr(state);

            ExpectTokenSL(state, TINY_TOK_CLOSEPAREN, "Expected matching ')' after previous '('");
            GetNextToken(state);

            Tiny_Expr *exp = Expr_create(TINY_EXP_PAREN, state);
            exp->paren = inner;
            return exp;
        } break;

        case TINY_TOK_NEW: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_CONSTRUCTOR, state);

            GetNextToken(state);

            Tiny_Symbol *tag = DeclareStruct(state, state->l.lexeme, true);

            exp->constructor.structTag = tag;

            exp->constructor.argNamesHead = NULL;
            Tiny_StringNode *argNamesTail = NULL;

            exp->constructor.argsHead = NULL;
            Tiny_Expr *argsTail = NULL;

            GetExpectTokenSL(state, TINY_TOK_OPENCURLY, "Expected '{' after struct name");

            GetNextToken(state);

            while (state->l.lastTok != TINY_TOK_CLOSECURLY) {
                if (state->l.lastTok == TINY_TOK_DOT) {
                    // Named argument, .xyz = value
                    GetExpectTokenSL(state, TINY_TOK_IDENT,
                                     "Expected identifier after '.' in designated initializer.");

                    Tiny_StringNode *ident = CreateExprStringNode(state, state->l.lexeme);

                    TINY_LL_APPEND(exp->constructor.argNamesHead, argNamesTail, ident);

                    GetExpectTokenSL(state, TINY_TOK_EQUAL,
                                     "Expected = after designated initializer");

                    GetNextToken(state);
                }

                Tiny_Expr *e = ParseExpr(state);

                TINY_LL_APPEND(exp->constructor.argsHead, argsTail, e);

                if (state->l.lastTok != TINY_TOK_CLOSECURLY && state->l.lastTok != TINY_TOK_COMMA) {
                    ReportErrorSL(state, "Expected '}' or ',' in constructor arg list.");
                }

                if (state->l.lastTok == TINY_TOK_COMMA) GetNextToken(state);
            }

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_CAST: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_CAST, state);

            GetExpectTokenSL(state, TINY_TOK_OPENPAREN, "Expected '(' after cast");

            GetNextToken(state);

            exp->cast.value = ParseExpr(state);

            ExpectTokenSL(state, TINY_TOK_COMMA, "Expected ',' after cast value");

            GetNextToken(state);

            exp->cast.tag = ParseTypeSL(state);

            ExpectTokenSL(state, TINY_TOK_CLOSEPAREN,
                          "Expected ')' to match previous '(' after cast.");

            GetNextToken(state);

            while (state->l.lastTok == TINY_TOK_DOT) {
                Tiny_Expr *e = Expr_create(TINY_EXP_DOT, state);

                GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after '.'");

                e->dot.lhs = exp;
                e->dot.field = CreateExprStringNode(state, state->l.lexeme);

                GetNextToken(state);

                exp = e;
            }

            return exp;
        } break;

        // This is effectively a ternary
        case TINY_TOK_IF: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_IF_TERNARY, state);

            GetNextToken(state);

            exp->ifx.cond = ParseExpr(state);

            // Body and else statement must evaluate to a value
            exp->ifx.body = ParseExpr(state);

            ExpectTokenSL(state, TINY_TOK_ELSE,
                          "Expected 'else' after true value in if expression");

            GetNextToken(state);

            exp->ifx.alt = ParseExpr(state);

            return exp;
        } break;

        case TINY_TOK_LEXER_ERROR: {
            assert(state->l.errorMsg);
            ReportErrorSL(state, state->l.errorMsg);
            return NULL;
        } break;

        default:
            break;
    }

    ReportErrorSL(state, "Unexpected token '%s'\n", state->l.lexeme);
    return NULL;
}

static Tiny_Expr *ParseSuffix(Tiny_State *state, Tiny_Expr *lhs) {
    Tiny_Expr *exp = lhs;

    while (state->l.lastTok == TINY_TOK_DOT || state->l.lastTok == TINY_TOK_ARROW ||
           state->l.lastTok == TINY_TOK_OPENSQUARE) {
        if (state->l.lastTok == TINY_TOK_OPENSQUARE) {
            // This is the "index" operator. Unlike the arrow operator below, we can't just
            // rewrite it as we're going here because it relies on the type of the expression
            // to generate the code correctly.
            GetNextToken(state);

            Tiny_Expr *e = Expr_create(TINY_EXP_INDEX, state);

            e->index.arr = exp;
            e->index.elem = ParseExpr(state);

            ExpectTokenSL(state, TINY_TOK_CLOSESQUARE, "Expected ']' after '[' and expression");
            GetNextToken(state);

            exp = e;
            continue;
        }

        if (state->l.lastTok == TINY_TOK_ARROW) {
            // There is always a call on the rhs of an arrow.
            GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after ->");

            // Since the arrow "operator" is just syntax sugar, we
            // actually rewrite the AST as we're producing it rather
            // than doing some transformation later.

            Tiny_StringNode *ident = CreateExprStringNode(state, state->l.lexeme);

            GetExpectTokenSL(state, TINY_TOK_OPENPAREN, "Expected '(' after -> and function name");

            Tiny_Expr *callExp = ParseCall(state, ident);

            callExp->lineNumber = exp->lineNumber;
            callExp->pos = exp->pos;

            // Prepend the expression on the lhs to the call expression on the rhs
            exp->next = callExp->call.argsHead;
            callExp->call.argsHead = exp;

            exp = callExp;

            continue;
        }

        Tiny_Expr *e = Expr_create(TINY_EXP_DOT, state);

        GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after '.'");

        e->dot.lhs = exp;
        e->dot.field = CreateExprStringNode(state, state->l.lexeme);

        GetNextToken(state);

        exp = e;
    }

    return exp;
}

static Tiny_Expr *ParseFactor(Tiny_State *state) {
    Tiny_Expr *exp = ParseValue(state);
    return ParseSuffix(state, exp);
}

static int GetTokenPrec(int tok) {
    int prec = -1;
    switch (tok) {
        case TINY_TOK_STAR:
        case TINY_TOK_SLASH:
        case TINY_TOK_PERCENT:
        case TINY_TOK_AND:
        case TINY_TOK_OR:
            prec = 5;
            break;

        case TINY_TOK_SHIFT_LEFT:
        case TINY_TOK_SHIFT_RIGHT:
        case TINY_TOK_PLUS:
        case TINY_TOK_MINUS:
            prec = 4;
            break;

        case TINY_TOK_LTE:
        case TINY_TOK_GTE:
        case TINY_TOK_EQUALS:
        case TINY_TOK_NOTEQUALS:
        case TINY_TOK_LT:
        case TINY_TOK_GT:
            prec = 3;
            break;

        case TINY_TOK_LOG_AND:
        case TINY_TOK_LOG_OR:
            prec = 2;
            break;
    }

    return prec;
}

static Tiny_Expr *ParseBinRhs(Tiny_State *state, int exprPrec, Tiny_Expr *lhs) {
    while (true) {
        int prec = GetTokenPrec(state->l.lastTok);

        if (prec < exprPrec) return lhs;

        int binOp = state->l.lastTok;

        GetNextToken(state);

        Tiny_Expr *rhs = ParseFactor(state);
        int nextPrec = GetTokenPrec(state->l.lastTok);

        if (prec < nextPrec) rhs = ParseBinRhs(state, prec + 1, rhs);

        Tiny_Expr *newLhs = Expr_create(TINY_EXP_BINARY, state);

        newLhs->binary.lhs = lhs;
        newLhs->binary.rhs = rhs;
        newLhs->binary.op = binOp;

        lhs = newLhs;
    }
}

static Tiny_Expr *ParseExpr(Tiny_State *state) {
    Tiny_Expr *factor = ParseFactor(state);
    return ParseBinRhs(state, 0, factor);
}

static Tiny_Expr *ParseStatement(Tiny_State *state) {
    switch (state->l.lastTok) {
        case TINY_TOK_OPENCURLY:
            return ParseBlock(state);

        case TINY_TOK_FUNC:
            return ParseFunc(state);

        case TINY_TOK_IF:
            return ParseIf(state);

        case TINY_TOK_WHILE: {
            GetNextToken(state);
            Tiny_Expr *exp = Expr_create(TINY_EXP_WHILE, state);

            exp->whilex.cond = ParseExpr(state);

            OpenScope(state);

            exp->whilex.body = ParseStatement(state);

            CloseScope(state);

            return exp;
        } break;

        case TINY_TOK_FOR: {
            GetNextToken(state);

            Tiny_Expr *exp = Expr_create(TINY_EXP_FOR, state);

            // Every local declared after this is scoped to the for
            OpenScope(state);

            exp->forx.init = ParseStatement(state);

            ExpectTokenSL(state, TINY_TOK_SEMI, "Expected ';' after for initializer.");

            GetNextToken(state);

            exp->forx.cond = ParseExpr(state);

            ExpectTokenSL(state, TINY_TOK_SEMI, "Expected ';' after for condition.");

            GetNextToken(state);

            exp->forx.step = ParseStatement(state);

            exp->forx.body = ParseStatement(state);

            CloseScope(state);

            return exp;
        } break;

        case TINY_TOK_RETURN: {
            if (!state->currFunc) {
                ReportErrorSL(state,
                              "Attempted to return from outside a function. Why? Why would "
                              "you do that? Why would you do any of that?");
            }

            Tiny_Expr *exp = Expr_create(TINY_EXP_RETURN, state);

            GetNextToken(state);
            if (state->l.lastTok == TINY_TOK_SEMI) {
                GetNextToken(state);
                exp->retExpr = NULL;
                return exp;
            }

            if (state->currFunc->func.returnTag->type == TINY_SYM_TAG_VOID) {
                ReportErrorSL(state,
                              "Attempted to return value from function which is "
                              "supposed to return nothing (void).");
            }

            exp->retExpr = ParseExpr(state);
            return exp;
        } break;

        // TODO(Apaar): Labeled break/continue
        case TINY_TOK_BREAK: {
            GetNextToken(state);
            Tiny_Expr *exp = Expr_create(TINY_EXP_BREAK, state);

            // Set to -1 to make sure we don't get into trouble
            exp->breakContinue.patchLoc = -1;

            return exp;
        } break;

        case TINY_TOK_CONTINUE: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_CONTINUE, state);

            GetNextToken(state);

            // Set to -1 to make sure we don't get into trouble
            exp->breakContinue.patchLoc = -1;

            return exp;
        } break;

        case TINY_TOK_USE: {
            if (state->currFunc || state->currScope != 0) {
                ReportErrorSL(state,
                              "'use' statements are only valid at the top-level of a Tiny file.");
            }

            Tiny_Expr *exp = Expr_create(TINY_EXP_USE, state);

            GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after 'use'");

            exp->use.moduleName = CreateExprStringNode(state, state->l.lexeme);

            exp->use.argsHead = NULL;
            Tiny_StringNode *argsTail = NULL;

            exp->use.asName = NULL;

            GetExpectTokenSL(state, TINY_TOK_OPENPAREN, "Expected '(' after 'use' module name");

            GetNextToken(state);

            while (state->l.lastTok != TINY_TOK_CLOSEPAREN) {
                ExpectTokenSL(state, TINY_TOK_STRING,
                              "Expected string as arg to 'use' module name");

                Tiny_StringNode *arg = CreateExprStringNode(state, state->l.lexeme);

                TINY_LL_APPEND(exp->use.argsHead, argsTail, arg);

                GetNextToken(state);

                // TODO(Apaar): Technically this means the commas are optional.
                // Should we enforce them?
                if (state->l.lastTok == TINY_TOK_COMMA) {
                    GetNextToken(state);
                }
            }

            GetNextToken(state);

            // NOTE(Apaar): We do not make `as` an official keyword, we only make a special case
            // for it in this context. It's too general to be reserved IMO.
            if (state->l.lastTok == TINY_TOK_IDENT && strcmp(state->l.lexeme, "as") == 0) {
                GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after 'as'");

                exp->use.asName = CreateExprStringNode(state, state->l.lexeme);

                GetNextToken(state);
            }

            return exp;
        } break;

        case TINY_TOK_FOREACH: {
            Tiny_Expr *exp = Expr_create(TINY_EXP_FOREACH, state);

            GetExpectTokenSL(state, TINY_TOK_IDENT, "Expected identifier after 'foreach'");

            // Every local declared after this is scoped to the foreach
            OpenScope(state);

            exp->forEach.elemVar = DeclareVar(state, state->l.lexeme);
            exp->forEach.indexVar = NULL;
            exp->forEach.reverse = false;
            exp->forEach.lenFunc = NULL;
            exp->forEach.getIndexFunc = NULL;

            GetNextToken(state);

            if (state->l.lastTok == TINY_TOK_COMMA) {
                GetExpectTokenSL(state, TINY_TOK_IDENT,
                                 "Expected identifier after 'foreach {varname},'");

                exp->forEach.indexVar = DeclareVar(state, state->l.lexeme);
                GetNextToken(state);
            } else {
                // Declare an anon var for the index
                exp->forEach.indexVar = DeclareVar(state, ANON_SYM_NAME);
            }

            const char *err = "Expected 'in' or 'in_reverse' after foreach ...";

            ExpectTokenSL(state, TINY_TOK_IDENT, err);

            if (strcmp(state->l.lexeme, "in_reverse") == 0) {
                exp->forEach.reverse = true;
            } else if (strcmp(state->l.lexeme, "in") != 0) {
                // It must be one of in or in_reverse
                ExpectTokenSL(state, TINY_TOK_IDENT, err);
            }

            GetNextToken(state);

            exp->forEach.range = ParseExpr(state);

            // You cannot refer to the range value
            //
            // TODO(Apaar): What if you could?? :)
            exp->forEach.rangeVar = DeclareVar(state, ANON_SYM_NAME);

            exp->forEach.body = ParseStatement(state);

            CloseScope(state);

            return exp;
        } break;

        default: {
            Tiny_Expr *lhs = ParseFactor(state);

            if (lhs->type == TINY_EXP_CALL) {
                // It ended up being call, so just return it
                return lhs;
            }

            int op = state->l.lastTok;

            int lineNumber = state->l.lineNumber;
            Tiny_TokenPos pos = state->l.pos;

            if (state->l.lastTok == TINY_TOK_DECLARE || state->l.lastTok == TINY_TOK_COLON) {
                if (lhs->type != TINY_EXP_ID) {
                    ReportErrorSL(state, "Left hand side of declaration must be identifier.");
                }

                lhs->id.sym = DeclareVar(state, lhs->id.name->value);

                if (state->l.lastTok == TINY_TOK_COLON) {
                    GetNextToken(state);
                    lhs->id.sym->var.tag = ParseTypeSL(state);

                    ExpectTokenSL(state, TINY_TOK_EQUAL, "Expected '=' after typename.");

                    op = TINY_TOK_EQUAL;
                }
            }

            // If the precedence is >= 0 then it's an expression operator
            if (GetTokenPrec(op) >= 0) {
                ReportErrorSL(state, "Expected assignment statement.");
            }

            GetNextToken(state);

            Tiny_Expr *rhs = ParseExpr(state);

            if (op == TINY_TOK_DECLARECONST) {
                if (lhs->type != TINY_EXP_ID) {
                    ReportErrorSL(state, "Left hand side of declaration must be identifier.");
                }

                if (rhs->type == TINY_EXP_BOOL) {
                    DeclareConst(state, lhs->id.name->value, GetPrimTag(TINY_SYM_TAG_BOOL))
                        ->constant.bValue = rhs->boolean;
                } else if (rhs->type == TINY_EXP_CHAR) {
                    DeclareConst(state, lhs->id.name->value, GetPrimTag(TINY_SYM_TAG_INT))
                        ->constant.iValue = rhs->iValue;
                } else if (rhs->type == TINY_EXP_INT) {
                    DeclareConst(state, lhs->id.name->value, GetPrimTag(TINY_SYM_TAG_INT))
                        ->constant.iValue = rhs->iValue;
                } else if (rhs->type == TINY_EXP_FLOAT) {
                    DeclareConst(state, lhs->id.name->value, GetPrimTag(TINY_SYM_TAG_FLOAT))
                        ->constant.fValue = rhs->fValue;
                } else if (rhs->type == TINY_EXP_STRING) {
                    DeclareConst(state, lhs->id.name->value, GetPrimTag(TINY_SYM_TAG_STR))
                        ->constant.sIndex = rhs->sIndex;
                } else {
                    ReportErrorSL(state, "Expected number or string to be bound to constant '%s'.",
                                  lhs->id.name->value);
                }
            }

            Tiny_Expr *bin = Expr_create(TINY_EXP_BINARY, state);

            bin->lineNumber = lineNumber;
            bin->pos = pos;

            bin->binary.lhs = lhs;
            bin->binary.rhs = rhs;
            bin->binary.op = op;

            return bin;
        } break;
    }

    ReportErrorSL(state, "Unexpected token '%s'.", state->l.lexeme);
    return NULL;
}

static Tiny_Expr *ParseProgram(Tiny_State *state) {
    GetNextToken(state);

    if (state->l.lastTok == TINY_TOK_EOF) {
        return NULL;
    }

    Tiny_Expr *head = NULL;
    Tiny_Expr *tail = NULL;

    while (state->l.lastTok != TINY_TOK_EOF) {
        if (state->l.lastTok == TINY_TOK_STRUCT) {
            ParseStruct(state);
        } else {
            TINY_LL_APPEND(head, tail, ParseStatement(state));
        }
    }

    return head;
}

static const char *GetTagName(const Tiny_Symbol *tag) {
    assert(tag);
    return tag->name;
}

static bool IsTagAssignableTo(const Tiny_Symbol *src, const Tiny_Symbol *dest) {
    if (src->type == TINY_SYM_TAG_VOID) {
        // Can't assign void to anything
        return false;
    }

    if (dest->type == TINY_SYM_TAG_ANY) {
        // Can always assign _to_ any
        return true;
    }

    if (src->type == TINY_SYM_TAG_ANY) {
        // Can only assign any to any
        return dest->type == TINY_SYM_TAG_ANY;
    }

    if (src->type == dest->type) {
        return strcmp(src->name, dest->name) == 0;
    }

    return false;
}

static void ResolveTypes(Tiny_State *state, Tiny_Expr *exp) {
    if (exp->tag) return;

    switch (exp->type) {
        case TINY_EXP_BREAK:
        case TINY_EXP_CONTINUE:
        case TINY_EXP_USE:
            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
            break;

        case TINY_EXP_NULL:
            exp->tag = GetPrimTag(TINY_SYM_TAG_ANY);
            break;
        case TINY_EXP_BOOL:
            exp->tag = GetPrimTag(TINY_SYM_TAG_BOOL);
            break;
        case TINY_EXP_CHAR:
            exp->tag = GetPrimTag(TINY_SYM_TAG_INT);
            break;
        case TINY_EXP_INT:
            exp->tag = GetPrimTag(TINY_SYM_TAG_INT);
            break;
        case TINY_EXP_FLOAT:
            exp->tag = GetPrimTag(TINY_SYM_TAG_FLOAT);
            break;
        case TINY_EXP_STRING:
            exp->tag = GetPrimTag(TINY_SYM_TAG_STR);
            break;

        case TINY_EXP_ID: {
            if (!exp->id.sym) {
                ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n",
                             exp->id.name->value);
            }

            assert(exp->id.sym->type == TINY_SYM_GLOBAL || exp->id.sym->type == TINY_SYM_LOCAL ||
                   exp->id.sym->type == TINY_SYM_CONST);

            if (exp->id.sym->type != TINY_SYM_CONST) {
                assert(exp->id.sym->var.tag);

                exp->tag = exp->id.sym->var.tag;
            } else {
                exp->tag = exp->id.sym->constant.tag;
            }
        } break;

        case TINY_EXP_CALL: {
            Tiny_Symbol *func = FindSymbol(state, exp->call.calleeName->value, ST_MASK_FUNC);

            if (!func) {
                ReportErrorE(state, exp, "Calling undeclared function '%s'.\n",
                             exp->call.calleeName->value);
            }

            int argc = func->type == TINY_SYM_FOREIGN_FUNCTION ? sb_count(func->foreignFunc.argTags)
                                                               : sb_count(func->func.args);

            bool isVarargs = func->type == TINY_SYM_FOREIGN_FUNCTION && func->foreignFunc.varargs;

            int i = 0;
            for (Tiny_Expr *node = exp->call.argsHead; node; (node = node->next, ++i)) {
                ResolveTypes(state, node);

                const Tiny_Symbol *expectedArgTag = NULL;

                if (i >= argc && !isVarargs) {
                    ReportErrorE(state, node, "Too many arguments to function '%s' (%d expected).",
                                 exp->call.calleeName->value, argc);
                }

                if (func->type == TINY_SYM_FOREIGN_FUNCTION) {
                    if (i < argc) {
                        expectedArgTag = func->foreignFunc.argTags[i];
                    }
                } else {
                    expectedArgTag = func->func.args[i]->var.tag;
                }

                if (!expectedArgTag) {
                    continue;
                }

                if (!IsTagAssignableTo(node->tag, expectedArgTag)) {
                    ReportErrorE(
                        state, node,
                        "Argument %i to '%s' is supposed to be a '%s' but you supplied a '%s'\n",
                        i + 1, func->name, GetTagName(expectedArgTag), GetTagName(node->tag));
                }
            }

            if (i < argc) {
                ReportErrorE(state, exp, "'%s' expects (at least) %d args but you supplied %d",
                             exp->call.calleeName->value, argc, i);
            }

            exp->tag = func->type == TINY_SYM_FOREIGN_FUNCTION ? func->foreignFunc.returnTag
                                                               : func->func.returnTag;
        } break;

        case TINY_EXP_PAREN: {
            ResolveTypes(state, exp->paren);

            exp->tag = exp->paren->tag;
        } break;

        case TINY_EXP_BINARY: {
            switch (exp->binary.op) {
                case TINY_TOK_PLUS:
                case TINY_TOK_MINUS:
                case TINY_TOK_STAR:
                case TINY_TOK_SLASH: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = exp->binary.lhs->tag->type == TINY_SYM_TAG_INT;
                    bool iRhs = exp->binary.rhs->tag->type == TINY_SYM_TAG_INT;

                    bool fLhs = !iLhs && exp->binary.lhs->tag->type == TINY_SYM_TAG_FLOAT;
                    bool fRhs = !iRhs && exp->binary.rhs->tag->type == TINY_SYM_TAG_FLOAT;

                    if ((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary op must be ints or "
                                     "floats, but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag((iLhs && iRhs) ? TINY_SYM_TAG_INT : TINY_SYM_TAG_FLOAT);
                } break;

                case TINY_TOK_AND:
                case TINY_TOK_OR:
                case TINY_TOK_PERCENT:
                case TINY_TOK_SHIFT_LEFT:
                case TINY_TOK_SHIFT_RIGHT: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = exp->binary.lhs->tag->type == TINY_SYM_TAG_INT;
                    bool iRhs = exp->binary.rhs->tag->type == TINY_SYM_TAG_INT;

                    if (!(iLhs && iRhs)) {
                        ReportErrorE(
                            state, exp,
                            "Both sides of binary op must be ints, but they're %s and %s\n",
                            GetTagName(exp->binary.lhs->tag), GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_INT);
                } break;

                case TINY_TOK_LOG_AND:
                case TINY_TOK_LOG_OR: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (exp->binary.lhs->tag->type != TINY_SYM_TAG_BOOL ||
                        exp->binary.rhs->tag->type != TINY_SYM_TAG_BOOL) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary and/or must be bools, "
                                     "but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_BOOL);
                } break;

                case TINY_TOK_GT:
                case TINY_TOK_LT:
                case TINY_TOK_LTE:
                case TINY_TOK_GTE: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = exp->binary.lhs->tag->type == TINY_SYM_TAG_INT;
                    bool iRhs = exp->binary.rhs->tag->type == TINY_SYM_TAG_INT;

                    bool fLhs = !iLhs && exp->binary.lhs->tag->type == TINY_SYM_TAG_FLOAT;
                    bool fRhs = !iRhs && exp->binary.rhs->tag->type == TINY_SYM_TAG_FLOAT;

                    if ((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary comparison must be "
                                     "ints or floats, but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_BOOL);
                } break;

                case TINY_TOK_EQUALS:
                case TINY_TOK_NOTEQUALS: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (exp->binary.lhs->tag->type == TINY_SYM_TAG_VOID ||
                        exp->binary.rhs->tag->type == TINY_SYM_TAG_VOID) {
                        ReportErrorE(
                            state, exp,
                            "Attempted to check for equality with void. This is not allowed.");
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_BOOL);
                } break;

                case TINY_TOK_DECLARE: {
                    assert(exp->binary.lhs->type == TINY_EXP_ID);

                    assert(exp->binary.lhs->id.sym);

                    ResolveTypes(state, exp->binary.rhs);

                    if (exp->binary.rhs->tag->type == TINY_SYM_TAG_VOID) {
                        ReportErrorE(state, exp,
                                     "Attempted to initialize variable with void expression. "
                                     "Don't do that.");
                    }

                    exp->binary.lhs->id.sym->var.tag = exp->binary.rhs->tag;

                    exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
                } break;

                case TINY_TOK_PLUSEQUAL:
                case TINY_TOK_MINUSEQUAL:
                case TINY_TOK_STAREQUAL:
                case TINY_TOK_SLASHEQUAL:
                case TINY_TOK_PERCENTEQUAL:
                case TINY_TOK_OREQUAL:
                case TINY_TOK_ANDEQUAL:
                case TINY_TOK_EQUAL: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (!IsTagAssignableTo(exp->binary.rhs->tag, exp->binary.lhs->tag)) {
                        ReportErrorE(
                            state, exp, "Attempted to assign a '%s' to a '%s'. Can't do that.",
                            GetTagName(exp->binary.rhs->tag), GetTagName(exp->binary.lhs->tag));
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
                } break;

                default: {
                    ResolveTypes(state, exp->binary.rhs);
                    exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
                } break;
            }
        } break;

        case TINY_EXP_UNARY: {
            ResolveTypes(state, exp->unary.exp);

            switch (exp->unary.op) {
                case TINY_TOK_MINUS: {
                    bool i = exp->unary.exp->tag->type == TINY_SYM_TAG_INT;
                    bool f = !i && exp->unary.exp->tag->type == TINY_SYM_TAG_FLOAT;

                    if (!(i || f)) {
                        ReportErrorE(state, exp, "Attempted to apply unary '-' to a %s.",
                                     GetTagName(exp->unary.exp->tag));
                    }

                    exp->tag = i ? GetPrimTag(TINY_SYM_TAG_INT) : GetPrimTag(TINY_SYM_TAG_FLOAT);
                } break;

                case TINY_TOK_BANG: {
                    if (exp->unary.exp->tag->type != TINY_SYM_TAG_BOOL) {
                        ReportErrorE(state, exp, "Attempted to apply unary 'not' to a %s.",
                                     GetTagName(exp->unary.exp->tag));
                    }

                    exp->tag = GetPrimTag(TINY_SYM_TAG_BOOL);
                } break;

                default:
                    assert(0);
                    break;
            }
        } break;

        case TINY_EXP_BLOCK: {
            for (Tiny_Expr *node = exp->blockHead; node; node = node->next) {
                ResolveTypes(state, node);
            }

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_PROC: {
            Tiny_Symbol *prevCurrFunc = state->currFunc;
            state->currFunc = exp->proc.decl;

            ResolveTypes(state, exp->proc.body);

            state->currFunc = prevCurrFunc;

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_IF: {
            ResolveTypes(state, exp->ifx.cond);

            if (exp->ifx.cond->tag->type != TINY_SYM_TAG_BOOL) {
                ReportErrorE(state, exp, "If condition is supposed to be a bool but its a %s",
                             GetTagName(exp->ifx.cond->tag));
            }

            ResolveTypes(state, exp->ifx.body);

            if (exp->ifx.alt) {
                ResolveTypes(state, exp->ifx.alt);
            }

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_RETURN: {
            assert(state->currFunc);

            if (exp->retExpr) {
                ResolveTypes(state, exp->retExpr);

                if (!IsTagAssignableTo(exp->retExpr->tag, state->currFunc->func.returnTag)) {
                    ReportErrorE(
                        state, exp,
                        "You tried to return a '%s' from function '%s' but its return type is '%s'",
                        GetTagName(exp->retExpr->tag), state->currFunc->name,
                        GetTagName(state->currFunc->func.returnTag));
                }
            } else if (state->currFunc->func.returnTag->type != TINY_SYM_TAG_VOID) {
                ReportErrorE(state, exp,
                             "Attempted to return without value in function '%s' even though its "
                             "return type is %s",
                             state->currFunc->name, GetTagName(state->currFunc->func.returnTag));
            }

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_WHILE: {
            ResolveTypes(state, exp->whilex.cond);

            if (exp->whilex.cond->tag->type != TINY_SYM_TAG_BOOL) {
                ReportErrorE(state, exp, "While condition is supposed to be a bool but its a %s",
                             GetTagName(exp->whilex.cond->tag));
            }

            ResolveTypes(state, exp->whilex.body);

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_FOR: {
            ResolveTypes(state, exp->forx.init);
            ResolveTypes(state, exp->forx.cond);

            if (exp->forx.cond->tag->type != TINY_SYM_TAG_BOOL) {
                ReportErrorE(state, exp, "For condition is supposed to be a bool but its a %s",
                             GetTagName(exp->forx.cond->tag));
            }

            ResolveTypes(state, exp->forx.step);
            ResolveTypes(state, exp->forx.body);

            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);
        } break;

        case TINY_EXP_DOT: {
            ResolveTypes(state, exp->dot.lhs);

            if (exp->dot.lhs->tag->type != TINY_SYM_TAG_STRUCT) {
                ReportErrorE(state, exp, "Cannot use '.' on a %s", GetTagName(exp->dot.lhs->tag));
            }

            exp->tag = GetFieldTag(exp->dot.lhs->tag, exp->dot.field->value, NULL);

            if (!exp->tag) {
                ReportErrorE(state, exp, "Struct %s doesn't have a field named %s",
                             GetTagName(exp->dot.lhs->tag), exp->dot.field->value);
            }
        } break;

        case TINY_EXP_CONSTRUCTOR: {
            assert(exp->constructor.structTag->sstruct.defined);

            int tagCount = sb_count(exp->constructor.structTag->sstruct.fields);

            Tiny_StringNode *nameNode = exp->constructor.argNamesHead;

            int i = 0;
            for (Tiny_Expr *argNode = exp->constructor.argsHead; argNode;
                 (argNode = argNode->next, ++i)) {
                ResolveTypes(state, argNode);

                if (!nameNode && exp->constructor.argNamesHead) {
                    ReportErrorE(state, exp,
                                 "Invalid designated initializer for struct %s. Make sure you "
                                 "initialize every field.",
                                 GetTagName(exp->constructor.structTag));
                }

                if (i >= tagCount) {
                    ReportErrorE(state, exp,
                                 "struct %s constructor expects %d args but you supplied more.",
                                 GetTagName(exp->constructor.structTag), tagCount);
                }

                if (exp->constructor.argNamesHead) {
                    bool found = false;

                    for (int j = 0; j < tagCount; ++j) {
                        Tiny_Symbol *expectedField = exp->constructor.structTag->sstruct.fields[j];
                        assert(expectedField->type == TINY_SYM_FIELD);

                        if (strcmp(nameNode->value, expectedField->name) != 0) {
                            continue;
                        }

                        if (!IsTagAssignableTo(argNode->tag, expectedField->fieldTag)) {
                            ReportErrorE(state, argNode,
                                         "Designated initializer .%s to constructor is supposed to "
                                         "be a %s but "
                                         "you supplied a %s",
                                         expectedField->name, GetTagName(expectedField->fieldTag),
                                         GetTagName(argNode->tag));
                        }

                        found = true;
                        break;
                    }

                    if (!found) {
                        ReportErrorE(state, argNode,
                                     "Designated initializer .%s doesn't correspond to any"
                                     "field on struct %s",
                                     nameNode->value, GetTagName(exp->constructor.structTag));
                    }
                } else {
                    Tiny_Symbol *expectedField = exp->constructor.structTag->sstruct.fields[i];
                    assert(expectedField->type == TINY_SYM_FIELD);

                    if (!IsTagAssignableTo(argNode->tag, expectedField->fieldTag)) {
                        ReportErrorE(
                            state, argNode,
                            "Initializer %i (for field '%s') to constructor is supposed to "
                            "be a %s but "
                            "you supplied a %s",
                            i + 1, expectedField->name, GetTagName(expectedField->fieldTag),
                            GetTagName(argNode->tag));
                    }
                }

                if (nameNode) {
                    nameNode = nameNode->next;
                }
            }

            exp->tag = exp->constructor.structTag;
        } break;

        case TINY_EXP_CAST: {
            assert(exp->cast.value);
            assert(exp->cast.tag);

            ResolveTypes(state, exp->cast.value);

            // TODO(Apaar): Allow casting of int to float etc

            // Allow casting from any or to any, but nothing else
            if (exp->cast.value->tag->type != TINY_SYM_TAG_ANY &&
                exp->cast.tag->type != TINY_SYM_TAG_ANY) {
                ReportErrorE(state, exp->cast.value, "Attempted to cast a %s; only any is allowed.",
                             GetTagName(exp->cast.value->tag));
            }

            exp->tag = exp->cast.tag;
        } break;

        case TINY_EXP_IF_TERNARY: {
            assert(exp->ifx.cond);
            assert(exp->ifx.body);
            assert(exp->ifx.alt);

            ResolveTypes(state, exp->ifx.cond);

            if (exp->ifx.cond->tag->type != TINY_SYM_TAG_BOOL) {
                ReportErrorE(state, exp,
                             "Ternary if condition is supposed to be a bool but its a %s",
                             GetTagName(exp->ifx.cond->tag));
            }

            ResolveTypes(state, exp->ifx.body);
            ResolveTypes(state, exp->ifx.alt);

            // TODO(Apaar): We assume symbols are interned here
            if (exp->ifx.body->tag != exp->ifx.alt->tag) {
                ReportErrorE(
                    state, exp,
                    "Ternary if 'true' value type '%s' is different from the false value type '%s'",
                    GetTagName(exp->ifx.body->tag), GetTagName(exp->ifx.alt->tag));
            }

            exp->tag = exp->ifx.body->tag;
        } break;

        case TINY_EXP_INDEX: {
            ResolveTypes(state, exp->index.arr);
            ResolveTypes(state, exp->index.elem);

            const char *arrTypeName = GetTagName(exp->index.arr->tag);

            // TODO(Apaar): Allow for longer names??
            char buf[256] = {0};
            snprintf(buf, sizeof(buf), "%s_get_index", arrTypeName);

            // This better exist (registered or otherwise)
            const Tiny_Symbol *getIndexFunc = FindSymbol(state, buf, ST_MASK_FUNC);

            if (!getIndexFunc) {
                ReportErrorE(state, exp->index.arr,
                             "In order to use [] you must define %s, but no such function exists",
                             buf);
            }

            Tiny_Symbol *getIndexReturnType = getIndexFunc->type == TINY_SYM_FOREIGN_FUNCTION
                                                  ? getIndexFunc->foreignFunc.returnTag
                                                  : getIndexFunc->func.returnTag;

            // TODO(Apaar): Check that the argument count of getIndexFunc is 2 and also that it
            // receives the correct argument types.

            exp->tag = getIndexReturnType;

            snprintf(buf, sizeof(buf), "%s_set_index", arrTypeName);

            // This one may or may not exist. Only matters if used.
            // TODO(Apaar): If it does exist, we should ensure it is symmetric with the getIndexFunc
            const Tiny_Symbol *setIndexFunc = FindSymbol(state, buf, ST_MASK_FUNC);

            exp->index.getIndexFunc = getIndexFunc;
            exp->index.setIndexFunc = setIndexFunc;
        } break;

        case TINY_EXP_FOREACH: {
            exp->tag = GetPrimTag(TINY_SYM_TAG_VOID);

            ResolveTypes(state, exp->forEach.range);

            // Must resolve the types of the elemVar and indexVar before resolving the body
            // because it probably uses these vars
            exp->forEach.indexVar->var.tag = GetPrimTag(TINY_SYM_TAG_INT);

            {
                const char *arrTypeName = GetTagName(exp->forEach.range->tag);

                // HACK(Apaar): Copy pasted wholesale from above
                // COPYPASTE BEGIN

                // TODO(Apaar): Allow for longer names??
                char buf[256] = {0};
                snprintf(buf, sizeof(buf), "%s_get_index", arrTypeName);

                // This better exist (registered or otherwise)
                const Tiny_Symbol *getIndexFunc = FindSymbol(state, buf, ST_MASK_FUNC);

                if (!getIndexFunc) {
                    ReportErrorE(state, exp->forEach.range,
                                 "In order to use `foreach` on this you must define %s, but no "
                                 "such function exists",
                                 buf, arrTypeName);
                }

                Tiny_Symbol *getIndexReturnType = getIndexFunc->type == TINY_SYM_FOREIGN_FUNCTION
                                                      ? getIndexFunc->foreignFunc.returnTag
                                                      : getIndexFunc->func.returnTag;

                // TODO(Apaar): Check that the argument count of getIndexFunc is 2 and also that it
                // receives the correct argument types.

                // COPYPASTE END

                snprintf(buf, sizeof(buf), "%s_len", arrTypeName);

                // This better exist (registered or otherwise)
                const Tiny_Symbol *lenFunc = FindSymbol(state, buf, ST_MASK_FUNC);

                if (!lenFunc) {
                    ReportErrorE(state, exp->forEach.range,
                                 "In order to use `foreach` on this you must define %s, but no "
                                 "such function exists",
                                 buf);
                }

                exp->forEach.getIndexFunc = getIndexFunc;
                exp->forEach.lenFunc = lenFunc;
                exp->forEach.elemVar->var.tag = getIndexReturnType;
            }

            ResolveTypes(state, exp->forEach.body);
        } break;
    }
}

static void CompileProgram(Tiny_State *state, Tiny_Expr *program);

static Tiny_ConstantIndex GenerateJump(Tiny_State *state, Word op, Tiny_ConstantIndex dest) {
    assert(op == TINY_OP_GOTO || op == TINY_OP_GOTOZ);

    GenerateCode(state, op);

    int pos = 0;
    GEN_VALUE(state, &dest, &pos);

    return pos;
}

static void PatchJumpLoc(Tiny_State *state, Tiny_ConstantIndex pos, Tiny_ConstantIndex patchValue) {
    GEN_VALUE_AT(state, &patchValue, pos);
}

static void GeneratePushInt(Tiny_State *state, Tiny_Int iValue) {
    if (iValue == 0) {
        GenerateCode(state, TINY_OP_PUSH_0);
    } else if (iValue == 1) {
        GenerateCode(state, TINY_OP_PUSH_1);
    } else {
        // TODO(Apaar): Add small integer optimization
        GenerateCode(state, TINY_OP_PUSH_INT);
        GEN_VALUE_NOPOS(state, &iValue);
    }
}

static void GeneratePushFloat(Tiny_State *state, Tiny_Float fValue) {
    GenerateCode(state, TINY_OP_PUSH_FLOAT);

    int pos = 0;
    GEN_VALUE(state, &fValue, &pos);
}

static void GeneratePushString(Tiny_State *state, Tiny_ConstantIndex sIndex) {
    if (sIndex <= 0xff) {
        GenerateCode(state, TINY_OP_PUSH_STRING_FF);
        GenerateCode(state, (Word)sIndex);
    } else {
        GenerateCode(state, TINY_OP_PUSH_STRING);
        GEN_VALUE_NOPOS(state, &sIndex);
    }
}

static void CompileExpr(Tiny_State *state, Tiny_Expr *exp);

static void CompileCallSymbolWithArgsPrepared(Tiny_State *state, Word nargs, const Tiny_Symbol *fn,
                                              Tiny_Expr *errorExp) {
    assert(fn->type == TINY_SYM_FOREIGN_FUNCTION || fn->type == TINY_SYM_FUNCTION);

    if (fn->type == TINY_SYM_FOREIGN_FUNCTION) {
        GenerateCode(state, TINY_OP_CALLF);

        int fNargs = sb_count(fn->foreignFunc.argTags);

        if (!(fn->foreignFunc.varargs && nargs >= fNargs) && fNargs != nargs) {
            ReportErrorE(state, errorExp, "Function '%s' expects %s%d args but you supplied %d.\n",
                         fn->name, fn->foreignFunc.varargs ? "at least " : "", fNargs, nargs);
        }

        GenerateCode(state, (Word)nargs);
        GEN_VALUE_NOPOS(state, &fn->foreignFunc.index);
    } else {
        GenerateCode(state, TINY_OP_CALL);
        GenerateCode(state, (Word)nargs);
        GEN_VALUE_NOPOS(state, &fn->func.index);
    }
}

static void CompileGetVar(Tiny_State *state, const Tiny_Symbol *sym) {
    assert(sym);
    assert(sym->type == TINY_SYM_CONST || sym->type == TINY_SYM_LOCAL ||
           sym->type == TINY_SYM_GLOBAL);

    if (sym->type == TINY_SYM_CONST) {
        if (sym->constant.tag == GetPrimTag(TINY_SYM_TAG_STR)) {
            GeneratePushString(state, sym->constant.sIndex);
        } else if (sym->constant.tag == GetPrimTag(TINY_SYM_TAG_BOOL)) {
            GenerateCode(state, sym->constant.bValue ? TINY_OP_PUSH_TRUE : TINY_OP_PUSH_FALSE);
        } else if (sym->constant.tag == GetPrimTag(TINY_SYM_TAG_INT)) {
            GeneratePushInt(state, sym->constant.iValue);
        } else if (sym->constant.tag == GetPrimTag(TINY_SYM_TAG_FLOAT)) {
            GeneratePushFloat(state, sym->constant.fValue);
        } else {
            assert(0);
        }

        return;
    }

    if (sym->type == TINY_SYM_GLOBAL) {
        GenerateCode(state, TINY_OP_GET);
        GEN_VALUE_NOPOS(state, &sym->var.index);
    } else {
        if (sym->var.index < 0 || sym->var.index > 0xff) {
            GenerateCode(state, TINY_OP_GETLOCAL);
            GEN_VALUE_NOPOS(state, &sym->var.index);
        } else {
            GenerateCode(state, TINY_OP_GETLOCAL_W);
            GenerateCode(state, (Word)sym->var.index);
        }
    }
}

static void CompileGetLHS(Tiny_State *state, Tiny_Expr *exp) {
    if (exp->type == TINY_EXP_ID) {
        if (!exp->id.sym)
            ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n", exp->id.name);

        CompileGetVar(state, exp->id.sym);
    } else if (exp->type == TINY_EXP_INDEX) {
        // Types should be resolved, so this should exist already
        assert(exp->index.getIndexFunc);

        // Push arr, elem onto the stack
        CompileExpr(state, exp->index.arr);
        CompileExpr(state, exp->index.elem);

        CompileCallSymbolWithArgsPrepared(state, 2, exp->index.getIndexFunc, exp);
        GenerateCode(state, TINY_OP_GET_RETVAL);
    } else {
        assert(exp->type == TINY_EXP_DOT);

        assert(exp->dot.lhs);
        assert(exp->dot.field);
        assert(exp->dot.lhs->tag->type == TINY_SYM_TAG_STRUCT);

        int idx;

        GetFieldTag(exp->dot.lhs->tag, exp->dot.field->value, &idx);

        assert(idx >= 0 && idx <= UCHAR_MAX);

        CompileExpr(state, exp->dot.lhs);

        GenerateCode(state, TINY_OP_STRUCT_GET);
        GenerateCode(state, (Word)idx);
    }
}

static void CompileCall(Tiny_State *state, Tiny_Expr *exp) {
    assert(exp->type == TINY_EXP_CALL);

    int nargs = 0;
    for (Tiny_Expr *node = exp->call.argsHead; node; node = node->next) {
        if (nargs >= UCHAR_MAX) {
            ReportErrorE(state, exp, "Exceeded maximum number of arguments (%d).", UCHAR_MAX);
        }

        CompileExpr(state, node);
        ++nargs;
    }

    Tiny_Symbol *sym = FindSymbol(state, exp->call.calleeName->value, ST_MASK_FUNC);

    if (!sym) {
        ReportErrorE(state, exp, "Attempted to call undefined function '%s'.\n",
                     exp->call.calleeName);
    }

    if (nargs > UCHAR_MAX) {
        ReportErrorE(state, exp,
                     "Attempted to call function '%s' with %d args. Tiny supports calling "
                     "functions with %d args at most.",
                     exp->call.calleeName, nargs, UCHAR_MAX);
    }

    CompileCallSymbolWithArgsPrepared(state, (Word)nargs, sym, exp);
}

static void CompileExpr(Tiny_State *state, Tiny_Expr *exp) {
    switch (exp->type) {
        case TINY_EXP_NULL: {
            GenerateCode(state, TINY_OP_PUSH_NULL);
        } break;

        case TINY_EXP_ID:
        case TINY_EXP_DOT:
        case TINY_EXP_INDEX: {
            CompileGetLHS(state, exp);
        } break;

        case TINY_EXP_BOOL: {
            GenerateCode(state, exp->boolean ? TINY_OP_PUSH_TRUE : TINY_OP_PUSH_FALSE);
        } break;

        case TINY_EXP_INT:
        case TINY_EXP_CHAR: {
            GeneratePushInt(state, exp->iValue);
        } break;

        case TINY_EXP_FLOAT: {
            GeneratePushFloat(state, exp->fValue);
        } break;

        case TINY_EXP_STRING: {
            GeneratePushString(state, exp->sIndex);
        } break;

        case TINY_EXP_CALL: {
            CompileCall(state, exp);
            GenerateCode(state, TINY_OP_GET_RETVAL);
        } break;

        case TINY_EXP_CONSTRUCTOR: {
            assert(exp->constructor.structTag->sstruct.defined);

            if (exp->constructor.argNamesHead) {
                // Compile in the order of the struct definition
                // and search for the arg name at each step
                for (int i = 0; i < sb_count(exp->constructor.structTag->sstruct.fields); ++i) {
                    const char *fieldName = exp->constructor.structTag->sstruct.fields[i]->name;

                    Tiny_StringNode *nameNode = exp->constructor.argNamesHead;

                    for (Tiny_Expr *node = exp->constructor.argsHead; node; node = node->next) {
                        // The number of name nodes must match the number of argNames otherwise
                        // there's an issue in the parser
                        assert(nameNode);

                        if (strcmp(nameNode->value, fieldName) != 0) {
                            nameNode = nameNode->next;
                            continue;
                        }

                        CompileExpr(state, node);
                        break;
                    }
                }
            } else {
                for (Tiny_Expr *node = exp->constructor.argsHead; node; node = node->next) {
                    CompileExpr(state, node);
                }
            }

            GenerateCode(state, TINY_OP_PUSH_STRUCT);
            GenerateCode(state, sb_count(exp->constructor.structTag->sstruct.fields));
        } break;

        case TINY_EXP_CAST: {
            assert(exp->tag);
            assert(exp->cast.value);

            // TODO(Apaar): Once the cast actually does something, change this to
            // generate casting opcodes (ex. OP_INT_TO_FLOAT, OP_FLOAT_TO_INT, etc)
            CompileExpr(state, exp->cast.value);
        } break;

        case TINY_EXP_BINARY: {
            switch (exp->binary.op) {
                case TINY_TOK_PLUS: {
                    CompileExpr(state, exp->binary.lhs);

                    if (exp->binary.rhs->type == TINY_EXP_INT && exp->binary.rhs->iValue == 1) {
                        GenerateCode(state, TINY_OP_ADD1);
                    } else {
                        CompileExpr(state, exp->binary.rhs);
                        GenerateCode(state, TINY_OP_ADD);
                    }
                } break;

                case TINY_TOK_MINUS: {
                    CompileExpr(state, exp->binary.lhs);

                    if (exp->binary.rhs->type == TINY_EXP_INT && exp->binary.rhs->iValue == 1) {
                        GenerateCode(state, TINY_OP_SUB1);
                    } else {
                        CompileExpr(state, exp->binary.rhs);
                        GenerateCode(state, TINY_OP_SUB);
                    }
                } break;

                case TINY_TOK_STAR: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_MUL);
                } break;

                case TINY_TOK_SLASH: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_DIV);
                } break;

                case TINY_TOK_PERCENT: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_MOD);
                } break;

                case TINY_TOK_OR: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_OR);
                } break;

                case TINY_TOK_AND: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_AND);
                } break;

                case TINY_TOK_SHIFT_LEFT: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_SHIFT_LEFT);
                } break;

                case TINY_TOK_SHIFT_RIGHT: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_SHIFT_RIGHT);
                } break;

                case TINY_TOK_LT: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_LT);
                } break;

                case TINY_TOK_GT: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_GT);
                } break;

                case TINY_TOK_EQUALS: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_EQU);
                } break;

                case TINY_TOK_NOTEQUALS: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_EQU);
                    GenerateCode(state, TINY_OP_LOG_NOT);
                } break;

                case TINY_TOK_LTE: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_LTE);
                } break;

                case TINY_TOK_GTE: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_GTE);
                } break;

                case TINY_TOK_LOG_AND: {
                    CompileExpr(state, exp->binary.lhs);

                    // Don't even bother running the RHS of the &&
                    Tiny_ConstantIndex shortCircuitLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

                    // If we here here, the top of the stack will be whatever the result of this
                    // final call was, which fine
                    CompileExpr(state, exp->binary.rhs);

                    // Skip over the "push false"
                    Tiny_ConstantIndex exitLoc = GenerateJump(state, TINY_OP_GOTO, 0);
                    // We push a false in the event

                    PatchJumpLoc(state, shortCircuitLoc, sb_count(state->program));
                    GenerateCode(state, TINY_OP_PUSH_FALSE);

                    PatchJumpLoc(state, exitLoc, sb_count(state->program));
                } break;

                case TINY_TOK_LOG_OR: {
                    CompileExpr(state, exp->binary.lhs);

                    // Skip over to the RHS
                    Tiny_ConstantIndex nextExprLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

                    // Otherwise push true and jump out
                    GenerateCode(state, TINY_OP_PUSH_TRUE);

                    int exitLoc = GenerateJump(state, TINY_OP_GOTO, 0);

                    PatchJumpLoc(state, nextExprLoc, sb_count(state->program));

                    CompileExpr(state, exp->binary.rhs);

                    PatchJumpLoc(state, exitLoc, sb_count(state->program));
                } break;

                default:
                    ReportErrorE(state, exp, "Found assignment when expecting expression.\n");
                    break;
            }
        } break;

        case TINY_EXP_PAREN: {
            CompileExpr(state, exp->paren);
        } break;

        case TINY_EXP_UNARY: {
            switch (exp->unary.op) {
                case TINY_TOK_MINUS: {
                    if (exp->unary.exp->type == TINY_EXP_INT) {
                        GenerateCode(state, TINY_OP_PUSH_INT);

                        Tiny_Int iValue = -exp->unary.exp->iValue;
                        GEN_VALUE_NOPOS(state, &iValue);
                    } else {
                        CompileExpr(state, exp->unary.exp);

                        GenerateCode(state, TINY_OP_PUSH_INT);
                        Tiny_Int iValue = -1;
                        GEN_VALUE_NOPOS(state, &iValue);

                        GenerateCode(state, TINY_OP_MUL);
                    }
                } break;

                case TINY_TOK_BANG: {
                    CompileExpr(state, exp->unary.exp);
                    GenerateCode(state, TINY_OP_LOG_NOT);
                } break;

                default:
                    ReportErrorE(state, exp, "Unsupported unary operator %c (%d)\n", exp->unary.op,
                                 exp->unary.op);
                    break;
            }
        } break;

        case TINY_EXP_IF_TERNARY: {
            CompileExpr(state, exp->ifx.cond);

            Tiny_ConstantIndex elseLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

            CompileExpr(state, exp->ifx.body);

            Tiny_ConstantIndex exitLoc = GenerateJump(state, TINY_OP_GOTO, 0);

            PatchJumpLoc(state, elseLoc, sb_count(state->program));
            assert(exp->ifx.alt);

            CompileExpr(state, exp->ifx.alt);

            PatchJumpLoc(state, exitLoc, sb_count(state->program));
        } break;

        default:
            ReportErrorE(state, exp, "Got statement when expecting expression.\n");
            break;
    }
}

static void PatchBreakContinue(Tiny_State *state, Tiny_Expr *body, int breakPC, int continuePC) {
    // For convenience
    if (!body) {
        return;
    }

    // If this encounters a loop, it will stop recursing since the
    // break/continue is lexical (and we don't support nested break
    // yet).

    switch (body->type) {
        // TODO(Apaar): These are the only types of blocks that are not loops, right? right?
        case TINY_EXP_IF: {
            PatchBreakContinue(state, body->ifx.body, breakPC, continuePC);
            PatchBreakContinue(state, body->ifx.alt, breakPC, continuePC);
        } break;

        case TINY_EXP_BLOCK: {
            for (Tiny_Expr *node = body->blockHead; node; node = node->next) {
                PatchBreakContinue(state, node, breakPC, continuePC);
            }
        } break;

        case TINY_EXP_BREAK: {
            if (breakPC < 0) {
                ReportErrorE(
                    state, body,
                    "A break statement does not make sense here. It must be inside a loop.");
            }

            PatchJumpLoc(state, body->breakContinue.patchLoc, breakPC);
        } break;

        case TINY_EXP_CONTINUE: {
            if (continuePC < 0) {
                ReportErrorE(state, body,
                             "A continue statement does not make sense here. It must be "
                             "inside a loop.");
            }

            PatchJumpLoc(state, body->breakContinue.patchLoc, continuePC);
        } break;

        default:
            break;
    }
}

static void AddPCFileLineRecord(Tiny_State *state, const Tiny_PCToFileLine record) {
    if (sb_count(state->pcToFileLine) > 0) {
        Tiny_PCToFileLine *last = &state->pcToFileLine[sb_count(state->pcToFileLine) - 1];

        if (record.pc == last->pc) {
            // If the PC matches, just update it. This means that the previous
            // statement we compiled may have resulted in no code generated.
            last->line = record.line;
            last->fileStrIndex = record.fileStrIndex;
            return;
        }
    }

    sb_push(&state->ctx, state->pcToFileLine, record);
}

static void GetFileLineForPC(const Tiny_State *state, int pc, const char **fileName, int *line) {
    if (fileName) {
        *fileName = NULL;
    }
    if (line) {
        *line = 0;
    }

    if (pc < 0 || sb_count(state->pcToFileLine) == 0) {
        return;
    }

    int lo = 0;
    int hi = sb_count(state->pcToFileLine);
    int curIndex = 0;

    for (;;) {
        curIndex = (lo + hi) / 2;

        if (lo + 1 >= hi) {
            break;
        }

        int curPC = state->pcToFileLine[curIndex].pc;

        if (curPC < pc) {
            lo = curIndex;
            continue;
        }

        if (curPC > pc) {
            hi = curIndex;
            continue;
        }

        break;
    }

    Tiny_PCToFileLine pcToFileLine = state->pcToFileLine[curIndex];

    if (fileName && pcToFileLine.fileStrIndex >= 0) {
        assert(pcToFileLine.fileStrIndex < state->numStrings);

        *fileName = state->strings[pcToFileLine.fileStrIndex];
    }

    if (line) {
        *line = pcToFileLine.line;
    }
}

void Tiny_GetExecutingFileLine(const Tiny_StateThread *thread, const char **fileName, int *line) {
    GetFileLineForPC(thread->state, thread->pc, fileName, line);
}

static void CompileAssignVarToTopOfStack(Tiny_State *state, Tiny_Symbol *destVar,
                                         Tiny_Expr *errExp) {
    assert(destVar);

    if (destVar->type == TINY_SYM_GLOBAL) {
        GenerateCode(state, TINY_OP_SET);
        GEN_VALUE_NOPOS(state, &destVar->var.index);
    } else if (destVar->type == TINY_SYM_LOCAL) {
        GenerateCode(state, TINY_OP_SETLOCAL);
        GEN_VALUE_NOPOS(state, &destVar->var.index);
    } else {
        // Probably a constant, can't change it
        ReportErrorE(state, errExp, "Cannot assign to id '%s'.\n", destVar->name);
    }

    destVar->var.initialized = true;
}

static void CompileStatement(Tiny_State *state, Tiny_Expr *exp) {
    AddPCFileLineRecord(
        state,
        (Tiny_PCToFileLine){
            .pc = sb_count(state->program),
            .fileStrIndex = state->l.fileName ? RegisterString(state, state->l.fileName) : -1,
            .line = exp->lineNumber,
        });

    switch (exp->type) {
        case TINY_EXP_CALL: {
            CompileCall(state, exp);
        } break;

        case TINY_EXP_BLOCK: {
            for (Tiny_Expr *node = exp->blockHead; node; node = node->next) {
                CompileStatement(state, node);
            }
        } break;

        case TINY_EXP_BINARY: {
            switch (exp->binary.op) {
                case TINY_TOK_DECLARECONST:
                    // Constants generate no code
                    break;

                case TINY_TOK_EQUAL:
                case TINY_TOK_DECLARE:  // These two are handled identically in terms of code
                                        // generated

                case TINY_TOK_PLUSEQUAL:
                case TINY_TOK_MINUSEQUAL:
                case TINY_TOK_STAREQUAL:
                case TINY_TOK_SLASHEQUAL:
                case TINY_TOK_PERCENTEQUAL:
                case TINY_TOK_ANDEQUAL:
                case TINY_TOK_OREQUAL: {
                    if (exp->binary.lhs->type != TINY_EXP_ID &&
                        exp->binary.lhs->type != TINY_EXP_DOT &&
                        exp->binary.lhs->type != TINY_EXP_INDEX) {
                        ReportErrorE(state, exp,
                                     "LHS of assignment operation must be a variable, dot, or "
                                     "index expression");
                    }

                    if (exp->binary.lhs->type == TINY_EXP_INDEX) {
                        // When we call _set_index we need to ensure the array, and index
                        // are on the stack in that order before the value.
                        // Hence we compile them here.
                        CompileExpr(state, exp->binary.lhs->index.arr);
                        CompileExpr(state, exp->binary.lhs->index.elem);
                    }

                    switch (exp->binary.op) {
                        case TINY_TOK_PLUSEQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);

                            if (exp->binary.rhs->type == TINY_EXP_INT &&
                                exp->binary.rhs->iValue == 1) {
                                GenerateCode(state, TINY_OP_ADD1);
                            } else {
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_ADD);
                            }
                        } break;

                        case TINY_TOK_MINUSEQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);

                            if (exp->binary.rhs->type == TINY_EXP_INT &&
                                exp->binary.rhs->iValue == 1) {
                                GenerateCode(state, TINY_OP_SUB1);
                            } else {
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_SUB);
                            }
                        } break;

                        case TINY_TOK_STAREQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);
                            CompileExpr(state, exp->binary.rhs);
                            GenerateCode(state, TINY_OP_MUL);
                        } break;

                        case TINY_TOK_SLASHEQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);
                            CompileExpr(state, exp->binary.rhs);
                            GenerateCode(state, TINY_OP_DIV);
                        } break;

                        case TINY_TOK_PERCENTEQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);
                            CompileExpr(state, exp->binary.rhs);
                            GenerateCode(state, TINY_OP_MOD);
                        } break;

                        case TINY_TOK_ANDEQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);
                            CompileExpr(state, exp->binary.rhs);
                            GenerateCode(state, TINY_OP_AND);
                        } break;

                        case TINY_TOK_OREQUAL: {
                            CompileGetLHS(state, exp->binary.lhs);
                            CompileExpr(state, exp->binary.rhs);
                            GenerateCode(state, TINY_OP_OR);
                        } break;

                        default:
                            CompileExpr(state, exp->binary.rhs);
                            break;
                    }

                    if (exp->binary.lhs->type == TINY_EXP_ID) {
                        if (!exp->binary.lhs->id.sym) {
                            // The variable being referenced doesn't exist
                            ReportErrorE(state, exp, "Assigning to undeclared identifier '%s'.\n",
                                         exp->binary.lhs->id.name);
                        }

                        CompileAssignVarToTopOfStack(state, exp->binary.lhs->id.sym,
                                                     exp->binary.lhs);
                    } else if (exp->binary.lhs->type == TINY_EXP_INDEX) {
                        const Tiny_Symbol *setIndexFunc = exp->binary.lhs->index.setIndexFunc;

                        if (!setIndexFunc) {
                            ReportErrorE(state, exp->binary.lhs,
                                         "There is no %s_set_index function so you can't use this "
                                         "on the left hand side of an assignment",
                                         GetTagName(exp->binary.lhs->index.arr->tag));
                        }

                        // See above: we already have arr, elem, and rhs on the stack, so
                        // now we just call
                        CompileCallSymbolWithArgsPrepared(state, 3, setIndexFunc, exp);
                    } else {
                        assert(exp->binary.lhs->type == TINY_EXP_DOT);
                        assert(exp->binary.lhs->dot.lhs->tag->type == TINY_SYM_TAG_STRUCT);

                        int idx;

                        GetFieldTag(exp->binary.lhs->dot.lhs->tag,
                                    exp->binary.lhs->dot.field->value, &idx);

                        assert(idx >= 0 && idx <= UCHAR_MAX);

                        CompileExpr(state, exp->binary.lhs->dot.lhs);

                        GenerateCode(state, TINY_OP_STRUCT_SET);
                        GenerateCode(state, (Word)idx);
                    }
                } break;

                default:
                    ReportErrorE(state, exp, "Invalid operation when expecting statement.\n");
                    break;
            }
        } break;

        case TINY_EXP_PROC: {
            int skipFuncBodyLoc = GenerateJump(state, TINY_OP_GOTO, 0);

            state->functionPcs[exp->proc.decl->func.index] = sb_count(state->program);

            if (sb_count(exp->proc.decl->func.locals) > 0xff) {
                ReportErrorE(state, exp, "Exceeded maximum number of local variables (%d) allowed.",
                             0xff);
            }

            if (sb_count(exp->proc.decl->func.locals) > 0) {
                GenerateCode(state, TINY_OP_PUSH_NULL_N);
                GenerateCode(state, (Word)sb_count(exp->proc.decl->func.locals));
            }

            if (exp->proc.body) {
                CompileStatement(state, exp->proc.body);
            }

            GenerateCode(state, TINY_OP_RETURN);

            PatchJumpLoc(state, skipFuncBodyLoc, sb_count(state->program));
        } break;

        case TINY_EXP_IF: {
            CompileExpr(state, exp->ifx.cond);

            int skipBodyLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

            if (exp->ifx.body) CompileStatement(state, exp->ifx.body);

            if (exp->ifx.alt) {
                int exitLoc = GenerateJump(state, TINY_OP_GOTO, 0);

                PatchJumpLoc(state, skipBodyLoc, sb_count(state->program));

                CompileStatement(state, exp->ifx.alt);

                PatchJumpLoc(state, exitLoc, sb_count(state->program));
            } else {
                PatchJumpLoc(state, skipBodyLoc, sb_count(state->program));
            }
        } break;

        case TINY_EXP_WHILE: {
            int condPc = sb_count(state->program);

            CompileExpr(state, exp->whilex.cond);

            int skipBodyLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

            if (exp->whilex.body) CompileStatement(state, exp->whilex.body);

            GenerateJump(state, TINY_OP_GOTO, condPc);

            PatchJumpLoc(state, skipBodyLoc, sb_count(state->program));
            PatchBreakContinue(state, exp->whilex.body, sb_count(state->program), condPc);
        } break;

        case TINY_EXP_FOR: {
            CompileStatement(state, exp->forx.init);

            int condPc = sb_count(state->program);
            CompileExpr(state, exp->forx.cond);

            int skipBodyLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

            if (exp->forx.body) CompileStatement(state, exp->forx.body);

            int stepPc = sb_count(state->program);

            CompileStatement(state, exp->forx.step);

            GenerateJump(state, TINY_OP_GOTO, condPc);

            PatchJumpLoc(state, skipBodyLoc, sb_count(state->program));

            PatchBreakContinue(state, exp->forx.body, sb_count(state->program), stepPc);
        } break;

        case TINY_EXP_FOREACH: {
            assert(exp->forEach.lenFunc);
            assert(exp->forEach.getIndexFunc);

            // Assign to range expr
            CompileExpr(state, exp->forEach.range);
            CompileAssignVarToTopOfStack(state, exp->forEach.rangeVar, exp->forEach.range);

            if (exp->forEach.reverse) {
                // Initialize indexVar to len - 1

                // (index) := aint_len(range) - 1
                CompileGetVar(state, exp->forEach.rangeVar);
                CompileCallSymbolWithArgsPrepared(state, 1, exp->forEach.lenFunc, exp);
                GenerateCode(state, TINY_OP_GET_RETVAL);
                GenerateCode(state, TINY_OP_SUB1);

                CompileAssignVarToTopOfStack(state, exp->forEach.indexVar, exp);
            } else {
                GenerateCode(state, TINY_OP_PUSH_0);
                CompileAssignVarToTopOfStack(state, exp->forEach.indexVar, exp);
            }

            // Foreach cond
            int condPc = sb_count(state->program);

            // index
            CompileGetVar(state, exp->forEach.indexVar);

            if (!exp->forEach.reverse) {
                // len
                CompileGetVar(state, exp->forEach.rangeVar);
                CompileCallSymbolWithArgsPrepared(state, 1, exp->forEach.lenFunc, exp);
                GenerateCode(state, TINY_OP_GET_RETVAL);

                // <
                GenerateCode(state, TINY_OP_LT);
            } else {
                GenerateCode(state, TINY_OP_PUSH_0);
                GenerateCode(state, TINY_OP_GTE);
            }

            int skipBodyLoc = GenerateJump(state, TINY_OP_GOTOZ, 0);

            // Initialize the elem to the result of get index
            CompileGetVar(state, exp->forEach.rangeVar);
            CompileGetVar(state, exp->forEach.indexVar);
            CompileCallSymbolWithArgsPrepared(state, 2, exp->forEach.getIndexFunc, exp);
            GenerateCode(state, TINY_OP_GET_RETVAL);

            CompileAssignVarToTopOfStack(state, exp->forEach.elemVar, exp);

            if (exp->forEach.body) CompileStatement(state, exp->forEach.body);

            int stepPc = sb_count(state->program);

            // Foreach step
            CompileGetVar(state, exp->forEach.indexVar);
            GenerateCode(state, exp->forEach.reverse ? TINY_OP_SUB1 : TINY_OP_ADD1);
            CompileAssignVarToTopOfStack(state, exp->forEach.indexVar, exp);

            GenerateJump(state, TINY_OP_GOTO, condPc);

            PatchJumpLoc(state, skipBodyLoc, sb_count(state->program));

            PatchBreakContinue(state, exp->forEach.body, sb_count(state->program), stepPc);
        } break;

        case TINY_EXP_RETURN: {
            if (exp->retExpr) {
                CompileExpr(state, exp->retExpr);
                GenerateCode(state, TINY_OP_RETURN_VALUE);
            } else {
                GenerateCode(state, TINY_OP_RETURN);
            }
        } break;

        case TINY_EXP_BREAK:
        case TINY_EXP_CONTINUE: {
            exp->breakContinue.patchLoc = GenerateJump(state, TINY_OP_GOTO, 0);
        } break;

        case TINY_EXP_USE:
            // Ignore use statements
            break;

        default:
            ReportErrorE(state, exp,
                         "So this parsed successfully but when compiling I saw an expression where "
                         "I was expecting a statement.\n");
            break;
    }
}

static void CompileProgram(Tiny_State *state, Tiny_Expr *progHead) {
    for (Tiny_Expr *exp = progHead; exp; exp = exp->next) {
        CompileStatement(state, exp);
    }
}

// This will only check the symbols declared between firstSymIndex and lastSymIndex.
// This ensures that we only check whether the symbols in the current module are initialized.
static void CheckInitialized(Tiny_State *state, int firstSymIndex, int lastSymIndex) {
    const char *fmt = "Attempted to use uninitialized variable '%s'.\n";

    for (int i = firstSymIndex; i < lastSymIndex; ++i) {
        Tiny_Symbol *node = state->globalSymbols[i];

        assert(node->type != TINY_SYM_LOCAL);

        if (node->type == TINY_SYM_GLOBAL) {
            if (!node->var.initialized) {
                ReportErrorS(state, node, fmt, node->name);
            }
        } else if (node->type == TINY_SYM_FUNCTION) {
            // Only check locals, arguments are initialized implicitly
            for (int i = 0; i < sb_count(node->func.locals); ++i) {
                Tiny_Symbol *local = node->func.locals[i];

                assert(local->type == TINY_SYM_LOCAL);

                if (!local->var.initialized) {
                    ReportErrorS(state, local, fmt, local->name);
                }
            }
        }
    }
}

// Goes through the registered symbols (GlobalSymbols) and assigns all foreign
// functions to their respective index in ForeignFunctions
static void BuildForeignFunctions(Tiny_State *state) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Tiny_Symbol *node = state->globalSymbols[i];

        if (node->type == TINY_SYM_FOREIGN_FUNCTION)
            state->foreignFunctions[node->foreignFunc.index] = node->foreignFunc.callee;
    }
}

static void CompileState(Tiny_State *state, Tiny_Expr *progHead) {
    // If this state was already compiled and it ends with an TINY_OP_HALT, We'll
    // just overwrite it
    if (sb_count(state->program) > 0) {
        if (state->program[sb_count(state->program) - 1] == TINY_OP_HALT) {
            stb__sbn(state->program) -= 1;
        }
    }

    for (Tiny_Expr *exp = progHead; exp; exp = exp->next) {
        ResolveTypes(state, exp);
    }

    // Allocate room for vm execution info

    // We realloc because this state might be compiled multiple times (if, e.g.,
    // Tiny_CompileString is called twice with same state)
    if (state->numFunctions > 0) {
        state->functionPcs =
            TRealloc(&state->ctx, state->functionPcs, state->numFunctions * sizeof(int));
    }

    if (state->numForeignFunctions > 0) {
        state->foreignFunctions =
            TRealloc(&state->ctx, state->foreignFunctions,
                     state->numForeignFunctions * sizeof(Tiny_ForeignFunction));
    }

    assert(state->numForeignFunctions == 0 || state->foreignFunctions);
    assert(state->numFunctions == 0 || state->functionPcs);

    BuildForeignFunctions(state);

    CompileProgram(state, progHead);
    GenerateCode(state, TINY_OP_HALT);
}

Tiny_CompileResult Tiny_CompileString(Tiny_State *state, const char *name, const char *string) {
    assert(state->compileCallNestCount < TINY_MAX_NESTED_COMPILE_CALLS);

    // In order to make this function re-entrant, we save the lexer/parser arena on the stack
    Tiny_Lexer prevLexer = state->l;
    Tiny_Arena prevParserArena = state->parserArena;

    Tiny_InitLexer(&state->l, name, string, state->ctx);
    Tiny_InitArena(&state->parserArena, state->ctx);

    int firstSymIndex = sb_count(state->globalSymbols);
    int startCodeLen = sb_count(state->program);

    // We have to do this _before_ setjmp because it'll get
    // incremented again if it jumps back and we put it after
    state->compileCallNestCount += 1;

    int jmpCode = setjmp(state->compileErrorJmpBufs[state->compileCallNestCount - 1]);

    if (jmpCode) {
        // Free all stuff allocated since the start of compilation
        for (int i = firstSymIndex; i < sb_count(state->globalSymbols); ++i) {
            Symbol_destroy(state->globalSymbols[i], &state->ctx);
        }

        if (state->globalSymbols) {
            stb__sbn(state->globalSymbols) = firstSymIndex;
        }

        // TOOD(Apaar): Can we just goto below?
        Tiny_DestroyLexer(&state->l);
        Tiny_DestroyArena(&state->parserArena);

        if (state->program) {
            // TODO(Apaar): Should we realloc and shrink?
            stb__sbn(state->program) = startCodeLen;
        }

        state->l = prevLexer;
        state->parserArena = prevParserArena;
        --state->compileCallNestCount;

        return state->compileErrorResult;
    }

    Tiny_Expr *progHead = ParseProgram(state);

    int lastSymIndex = sb_count(state->globalSymbols);

    // Just before we do into the type resolution state, apply all the module functions
    for (Tiny_Expr *exp = progHead; exp; exp = exp->next) {
        if (exp->type != TINY_EXP_USE) {
            continue;
        }

        // Find the module in the symbols
        bool found = false;

        for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
            Tiny_Symbol *s = state->globalSymbols[i];
            if (s->type == TINY_SYM_MODULE && strcmp(s->name, exp->use.moduleName->value) == 0) {
                // TODO(Apaar): Document this limit and assert above
                char *args[128] = {0};
                char **argp = args;

                for (Tiny_StringNode *node = exp->use.argsHead; node; node = node->next) {
                    if (argp - args > sizeof(args) / sizeof(args[0])) {
                        ReportErrorE(state, exp, "Macro %s takes too many args (limit is %d)",
                                     exp->use.moduleName->value, sizeof(args) / sizeof(args[0]));
                    }

                    *argp++ = node->value;
                }

                Tiny_MacroResult result =
                    s->modFunc(state, args, (int)(argp - args),
                               exp->use.asName ? exp->use.asName->value : NULL);

                if (result.type != TINY_MACRO_SUCCESS) {
                    ReportErrorE(state, exp, "'use' macro '%s' failed: %s",
                                 exp->use.moduleName->value, result.error.msg);
                }

                found = true;
                break;
            }
        }

        if (!found) {
            ReportErrorE(state, exp, "Attempted to reference undefined macro '%s'",
                         exp->use.moduleName->value);
        }
    }

    // Make sure all structs are defined
    for (int i = firstSymIndex; i < lastSymIndex; ++i) {
        Tiny_Symbol *s = state->globalSymbols[i];
        if (s->type == TINY_SYM_TAG_STRUCT && !s->sstruct.defined) {
            ReportErrorS(state, s, "Referenced undefined struct %s.", s->name);
        }
    }

    CompileState(state, progHead);

    CheckInitialized(state, firstSymIndex,
                     lastSymIndex);  // Done after compilation because it might have registered
                                     // undefined functions during the compilation stage

    Tiny_DestroyLexer(&state->l);

    // Deletes all the parser data in one go, no deletion chores required
    Tiny_DestroyArena(&state->parserArena);

    state->l = prevLexer;
    state->parserArena = prevParserArena;
    --state->compileCallNestCount;

    return (Tiny_CompileResult){.type = TINY_COMPILE_SUCCESS};
}

Tiny_CompileResult Tiny_CompileFile(Tiny_State *state, const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        Tiny_CompileResult result = {
            .type = TINY_COMPILE_ERROR,
        };

        snprintf(result.error.msg, sizeof(result.error.msg),
                 "Error: Unable to open file '%s' for reading\n", filename);
        return result;
    }

    fseek(file, 0, SEEK_END);

    long len = ftell(file);

    char *s = TMalloc(&state->ctx, len + 1);

    rewind(file);

    fread(s, 1, len, file);
    s[len] = 0;

    fclose(file);

    Tiny_CompileResult result = Tiny_CompileString(state, filename, s);

    TFree(&state->ctx, s);

    return result;
}

bool Tiny_DisasmOne(const Tiny_State *state, int *ppc, char *buf, size_t maxlen) {
    int pc = *ppc;

    if (pc < 0 || pc >= sb_count(state->program)) {
        snprintf(buf, maxlen, "PC out of bounds");
        return false;
    }

    const char *fname = NULL;
    int line = 0;

    GetFileLineForPC(state, pc, &fname, &line);

    int used = snprintf(buf, maxlen, "%d (%s:%d)\t", pc, fname, line);

    if (used >= maxlen) {
        return false;
    }

    buf += used;
    maxlen -= used;

    switch (state->program[pc]) {
#define OP_NO_ARGS(op)              \
    case TINY_OP_##op: {            \
        snprintf(buf, maxlen, #op); \
        ++pc;                       \
    } break;

        OP_NO_ARGS(PUSH_NULL)

        case TINY_OP_PUSH_NULL_N: {
            Word count = state->program[++pc];
            snprintf(buf, maxlen, "PUSH_NULL_N %d", count);
            ++pc;
        } break;

            OP_NO_ARGS(PUSH_TRUE)
            OP_NO_ARGS(PUSH_FALSE)
            OP_NO_ARGS(PUSH_0)
            OP_NO_ARGS(PUSH_1)

        case TINY_OP_PUSH_CHAR: {
            char ch = state->program[++pc];

            snprintf(buf, maxlen, "PUSH_CHAR '%c'", ch);
            ++pc;
        } break;

        case TINY_OP_PUSH_INT: {
            ++pc;

            Tiny_Int i = 0;
            READ_VALUE_AT(state, &pc, &i);

            snprintf(buf, maxlen, "PUSH_INT %lld", i);
        } break;

        case TINY_OP_PUSH_FLOAT: {
            ++pc;

            Tiny_Float f = 0;
            READ_VALUE_AT(state, &pc, &f);

            snprintf(buf, maxlen, "PUSH_FLOAT %g", f);
        } break;

        case TINY_OP_PUSH_STRING: {
            ++pc;

            Tiny_ConstantIndex stringIndex = 0;

            READ_VALUE_AT(state, &pc, &stringIndex);

            snprintf(buf, maxlen, "PUSH_STRING %d (\"%s\")", stringIndex,
                     state->strings[stringIndex]);
        } break;

        case TINY_OP_PUSH_STRING_FF: {
            ++pc;

            int stringIndex = (int)state->program[pc];

            snprintf(buf, maxlen, "PUSH_STRING_FF %d (\"%s\")", stringIndex,
                     state->strings[stringIndex]);
            ++pc;
        } break;

        case TINY_OP_PUSH_STRUCT: {
            ++pc;

            Word nFields = state->program[pc];
            assert(nFields > 0);

            snprintf(buf, maxlen, "PUSH_STRUCT %d", nFields);
            ++pc;
        } break;

        case TINY_OP_STRUCT_GET: {
            ++pc;

            Word i = state->program[pc];

            snprintf(buf, maxlen, "STRUCT_GET %d", i);
            ++pc;
        } break;

        case TINY_OP_STRUCT_SET: {
            ++pc;

            Word i = state->program[pc];

            snprintf(buf, maxlen, "STRUCT_SET %d", i);
            ++pc;
        } break;

            // TODO(Apaar): Refactor some of the instrs above to use this

            OP_NO_ARGS(ADD)
            OP_NO_ARGS(SUB)
            OP_NO_ARGS(MUL)
            OP_NO_ARGS(DIV)
            OP_NO_ARGS(MOD)
            OP_NO_ARGS(OR)
            OP_NO_ARGS(AND)
            OP_NO_ARGS(LT)
            OP_NO_ARGS(LTE)
            OP_NO_ARGS(GT)
            OP_NO_ARGS(GTE)

            OP_NO_ARGS(ADD1)
            OP_NO_ARGS(SUB1)

            OP_NO_ARGS(EQU)

            OP_NO_ARGS(LOG_NOT)

        case TINY_OP_SET: {
            ++pc;

            Tiny_ConstantIndex varIdx = 0;
            READ_VALUE_AT(state, &pc, &varIdx);

            // TODO(Apaar): Be helpful by searching global
            // symbols for this vars name.

            snprintf(buf, maxlen, "SET %d", varIdx);
        } break;

        case TINY_OP_GET: {
            ++pc;

            Tiny_ConstantIndex varIdx = 0;
            READ_VALUE_AT(state, &pc, &varIdx);

            snprintf(buf, maxlen, "GET %d", varIdx);
        } break;

        case TINY_OP_GOTO: {
            ++pc;

            Tiny_ConstantIndex newPC = 0;
            READ_VALUE_AT(state, &pc, &newPC);

            snprintf(buf, maxlen, "GOTO %d", newPC);
        } break;

        case TINY_OP_GOTOZ: {
            ++pc;

            Tiny_ConstantIndex newPC = 0;
            READ_VALUE_AT(state, &pc, &newPC);

            snprintf(buf, maxlen, "GOTOZ %d", newPC);
        } break;

        case TINY_OP_CALL: {
            ++pc;
            Word nargs = state->program[pc++];

            Tiny_ConstantIndex funcIdx = 0;
            READ_VALUE_AT(state, &pc, &funcIdx);

            // TODO(Apaar): Print function name too

            snprintf(buf, maxlen, "CALL %d %d (%d)", nargs, funcIdx, state->functionPcs[funcIdx]);
        } break;

            OP_NO_ARGS(RETURN)
            OP_NO_ARGS(RETURN_VALUE)

        case TINY_OP_CALLF: {
            ++pc;
            Word nargs = state->program[pc++];

            Tiny_ConstantIndex funcIdx = 0;
            READ_VALUE_AT(state, &pc, &funcIdx);

            const char *name = NULL;

            for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
                Tiny_Symbol *sym = state->globalSymbols[i];

                if (sym->type == TINY_SYM_FOREIGN_FUNCTION && sym->foreignFunc.index == funcIdx) {
                    name = sym->name;
                    break;
                }
            }

            // Can't print fptr with %p because that's only for
            // data pointers.
            snprintf(buf, maxlen, "CALLF %s(%d)", name, nargs);
        } break;

        case TINY_OP_GETLOCAL: {
            ++pc;

            Tiny_ConstantIndex varIdx = 0;
            READ_VALUE_AT(state, &pc, &varIdx);

            snprintf(buf, maxlen, "GETLOCAL %d", varIdx);
        } break;

        case TINY_OP_GETLOCAL_W: {
            ++pc;
            Word i = state->program[pc++];

            snprintf(buf, maxlen, "GETLOCAL_W %d", i);
        } break;

        case TINY_OP_SETLOCAL: {
            ++pc;

            Tiny_ConstantIndex varIdx = 0;
            READ_VALUE_AT(state, &pc, &varIdx);

            snprintf(buf, maxlen, "SETLOCAL %d", varIdx);
        } break;

            OP_NO_ARGS(GET_RETVAL)
            OP_NO_ARGS(HALT)
            OP_NO_ARGS(MISALIGNED_INSTRUCTION)

#undef OP_NO_ARGS

        default: {
            snprintf(buf, maxlen, "(unknown opcode %d)", state->program[pc]);
            return false;
        } break;
    }

    if (pc >= sb_count(state->program)) {
        *ppc = -1;
        return true;
    }

    *ppc = pc;
    return true;
}

Tiny_BindMacroResultType Tiny_BindMacro(Tiny_State *state, const char *name,
                                        Tiny_MacroFunction fn) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Tiny_Symbol *s = state->globalSymbols[i];
        if (s->type == TINY_SYM_MODULE && strcmp(s->name, name) == 0) {
            return TINY_BIND_MACRO_ERROR_DUPLICATE;
        }
    }

    Tiny_Symbol *newNode = Symbol_create(TINY_SYM_MODULE, name, state);

    newNode->modFunc = fn;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    return TINY_BIND_MACRO_SUCCESS;
}

size_t Tiny_SymbolArrayCount(Tiny_Symbol *const *arr) { return sb_count(arr); }

const Tiny_Symbol *Tiny_FindTypeSymbol(Tiny_State *state, const char *name) {
    const Tiny_Symbol *sym = GetTagFromName(state, name, false);

    // We shouldn't return undefined structs (this is an error)
    if (sym && sym->type == TINY_SYM_TAG_STRUCT && !sym->sstruct.defined) {
        return NULL;
    }

    return sym;
}

const Tiny_Symbol *Tiny_FindFuncSymbol(Tiny_State *state, const char *name) {
    return FindSymbol(state, name, ST_MASK_FUNC);
}

const Tiny_Symbol *Tiny_FindConstSymbol(Tiny_State *state, const char *name) {
    return FindSymbol(state, name, ST_MASK(TINY_SYM_CONST));
}

const char *Tiny_GetStringFromConstIndex(Tiny_State *state, Tiny_ConstantIndex sIndex) {
    assert(sIndex >= 0 && sIndex < state->numStrings);

    return state->strings[sIndex];
}

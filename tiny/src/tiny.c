// tiny.c -- an bytecode-based interpreter for the tiny language
#include "tiny.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detail.h"
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

static void *DefaultAlloc(void *ptr, size_t size, void *userdata) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    return realloc(ptr, size);
}

static Tiny_Context DefaultContext = {DefaultAlloc, NULL};

char *CloneString(Tiny_Context *ctx, const char *str) {
    size_t len = strlen(str);

    char *dup = TMalloc(ctx, len + 1);
    memcpy(dup, str, len + 1);

    return dup;
}

static void DeleteObject(Tiny_Context *ctx, Tiny_Object *obj) {
    if (obj->type == TINY_VAL_STRING)
        TFree(ctx, obj->string);
    else if (obj->type == TINY_VAL_NATIVE) {
        if (obj->nat.prop && obj->nat.prop->finalize) obj->nat.prop->finalize(obj->nat.addr);
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

    return value.obj->string;
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

static Tiny_Object *NewObject(Tiny_StateThread *thread, Tiny_ValueType type) {
    assert(type != TINY_VAL_STRUCT);

    Tiny_Object *obj = TMalloc(&thread->ctx, sizeof(Tiny_Object));

    obj->type = type;
    obj->next = thread->gcHead;
    thread->gcHead = obj;
    obj->marked = 0;

    thread->numObjects++;

    return obj;
}

static Tiny_Object *NewStructObject(Tiny_StateThread *thread, Word n) {
    assert(n >= 0);
    Tiny_Object *obj = TMalloc(&thread->ctx, sizeof(Tiny_Object) + sizeof(Tiny_Value) * n);

    obj->type = TINY_VAL_STRUCT;
    obj->next = thread->gcHead;
    thread->gcHead = obj;
    obj->marked = 0;

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

    Tiny_Object *obj = NewObject(thread, TINY_VAL_NATIVE);

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

Tiny_Value Tiny_NewInt(int i) {
    Tiny_Value val;

    val.type = TINY_VAL_INT;
    val.i = i;

    return val;
}

Tiny_Value Tiny_NewFloat(float f) {
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

Tiny_Value Tiny_NewString(Tiny_StateThread *thread, char *string) {
    assert(thread && thread->state && string);

    Tiny_Object *obj = NewObject(thread, TINY_VAL_STRING);
    obj->string = string;

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    return val;
}

static void Symbol_destroy(Symbol *sym, Tiny_Context *ctx);

static Tiny_Value Lib_ToInt(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewInt((int)Tiny_ToFloat(args[0]));
}

static Tiny_Value Lib_ToFloat(Tiny_StateThread *thread, const Tiny_Value *args, int count) {
    return Tiny_NewFloat((float)Tiny_ToInt(args[0]));
}

Tiny_State *Tiny_CreateStateWithContext(Tiny_Context ctx) {
    Tiny_State *state = TMalloc(&ctx, sizeof(Tiny_State));

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

    Tiny_BindFunction(state, "int(float): int", Lib_ToInt);
    Tiny_BindFunction(state, "float(int): float", Lib_ToFloat);

    return state;
}

Tiny_State *Tiny_CreateState(void) { return Tiny_CreateStateWithContext(DefaultContext); }

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

    thread->fileName = NULL;
    thread->lineNumber = -1;

    thread->userdata = NULL;
}

void Tiny_InitThread(Tiny_StateThread *thread, const Tiny_State *state) {
    Tiny_InitThreadWithContext(thread, state, DefaultContext);
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

static bool ExecuteCycle(Tiny_StateThread *thread);

int Tiny_GetGlobalIndex(const Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *sym = state->globalSymbols[i];

        if (sym->type == SYM_GLOBAL && strcmp(sym->name, name) == 0) {
            return sym->var.index;
        }
    }

    return -1;
}

int Tiny_GetFunctionIndex(const Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *sym = state->globalSymbols[i];

        if (sym->type == SYM_FUNCTION && strcmp(sym->name, name) == 0) {
            return sym->func.index;
        }
    }

    return -1;
}

static void DoPushIndir(Tiny_StateThread *thread, int nargs);
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

    const char *fileName = thread->fileName;
    int lineNumber = thread->lineNumber;

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
    while (thread->fc > fc && ExecuteCycle(thread))
        ;

    Tiny_Value newRetVal = thread->retVal;

    thread->pc = pc;
    thread->fp = fp;
    thread->sp = sp;
    thread->fc = fc;

    thread->fileName = fileName;
    thread->lineNumber = lineNumber;

    thread->retVal = retVal;

    return newRetVal;
}

bool Tiny_ExecuteCycle(Tiny_StateThread *thread) { return ExecuteCycle(thread); }

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

static int GenerateInt(Tiny_State *state, int value) {
    // TODO(Apaar): Don't hardcode alignment of int
    int padding = (sb_count(state->program) % 4 == 0) ? 0 : (4 - sb_count(state->program) % 4);

    for (int i = 0; i < padding; ++i) {
        GenerateCode(state, TINY_OP_MISALIGNED_INSTRUCTION);
    }

    int pos = sb_count(state->program);

    Word *wp = (Word *)(&value);
    for (int i = 0; i < sizeof(int); ++i) {
        GenerateCode(state, *wp++);
    }

    return pos;
}

static void GenerateIntAt(Tiny_State *state, int value, int pc) {
    // Must be aligned
    assert(pc % 4 == 0);

    Word *wp = (Word *)(&value);
    for (int i = 0; i < 4; ++i) {
        state->program[pc + i] = *wp++;
    }
}

static int RegisterString(Tiny_State *state, const char *string) {
    for (int i = 0; i < state->numStrings; ++i) {
        if (strcmp(state->strings[i], string) == 0) return i;
    }

    assert(state->numStrings < MAX_STRINGS);
    state->strings[state->numStrings++] = CloneString(&state->ctx, string);

    return state->numStrings - 1;
}

static Symbol *GetPrimTag(SymbolType type) {
    static Symbol prims[] = {{
                                 SYM_TAG_VOID,
                                 (char *)"void",
                             },
                             {
                                 SYM_TAG_BOOL,
                                 (char *)"bool",
                             },
                             {SYM_TAG_INT, (char *)"int"},
                             {SYM_TAG_FLOAT, (char *)"float"},
                             {SYM_TAG_STR, (char *)"str"},
                             {SYM_TAG_ANY, (char *)"any"}};

    return &prims[type - SYM_TAG_VOID];
}

static Symbol *Symbol_create(SymbolType type, const char *name, Tiny_State *state) {
    Symbol *sym = TMalloc(&state->ctx, sizeof(Symbol));

    sym->name = CloneString(&state->ctx, name);
    sym->type = type;
    sym->pos = state->l.pos;

    return sym;
}

static void Symbol_destroy(Symbol *sym, Tiny_Context *ctx) {
    if (sym->type == SYM_FUNCTION) {
        for (int i = 0; i < sb_count(sym->func.args); ++i) {
            Symbol *arg = sym->func.args[i];

            assert(arg->type == SYM_LOCAL);

            Symbol_destroy(arg, ctx);
        }

        sb_free(ctx, sym->func.args);

        for (int i = 0; i < sb_count(sym->func.locals); ++i) {
            Symbol *local = sym->func.locals[i];

            assert(local->type == SYM_LOCAL);

            Symbol_destroy(local, ctx);
        }

        sb_free(ctx, sym->func.locals);
    } else if (sym->type == SYM_FOREIGN_FUNCTION) {
        sb_free(ctx, sym->foreignFunc.argTags);
    } else if (sym->type == SYM_TAG_STRUCT) {
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
            Symbol *sym = state->currFunc->func.locals[i];

            assert(sym->type == SYM_LOCAL);

            if (sym->var.scope == state->currScope) {
                sym->var.scopeEnded = true;
            }
        }
    }

    --state->currScope;
}

static Symbol *ReferenceVariable(Tiny_State *state, const char *name) {
    if (state->currFunc) {
        // Check local variables
        for (int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
            Symbol *sym = state->currFunc->func.locals[i];

            assert(sym->type == SYM_LOCAL);

            // Make sure that it's available in the current scope too
            if (!sym->var.scopeEnded && strcmp(sym->name, name) == 0) {
                return sym;
            }
        }

        // Check arguments
        for (int i = 0; i < sb_count(state->currFunc->func.args); ++i) {
            Symbol *sym = state->currFunc->func.args[i];

            assert(sym->type == SYM_LOCAL);

            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
    }

    // Check global variables/constants
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *sym = state->globalSymbols[i];

        if (sym->type == SYM_GLOBAL || sym->type == SYM_CONST) {
            if (strcmp(sym->name, name) == 0) return sym;
        }
    }

    // This variable doesn't exist
    return NULL;
}

static void ReportError(Tiny_State *state, const char *s, ...);

static Symbol *DeclareGlobalVar(Tiny_State *state, const char *name) {
    Symbol *sym = ReferenceVariable(state, name);

    if (sym && (sym->type == SYM_GLOBAL || sym->type == SYM_CONST)) {
        ReportError(state,
                    "Attempted to declare multiple global entities with the same "
                    "name '%s'.",
                    name);
    }

    Symbol *newNode = Symbol_create(SYM_GLOBAL, name, state);

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
static Symbol *DeclareArgument(Tiny_State *state, const char *name, Symbol *tag, int nargs) {
    assert(state->currFunc);
    assert(tag);

    for (int i = 0; i < sb_count(state->currFunc->func.args); ++i) {
        Symbol *sym = state->currFunc->func.args[i];

        assert(sym->type == SYM_LOCAL);

        if (strcmp(sym->name, name) == 0) {
            ReportError(state, "Function '%s' takes multiple arguments with name '%s'.\n",
                        state->currFunc->name, name);
        }
    }

    Symbol *newNode = Symbol_create(SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = -nargs + sb_count(state->currFunc->func.args);
    newNode->var.scope = 0;  // These should be accessible anywhere in the function
    newNode->var.tag = tag;

    sb_push(&state->ctx, state->currFunc->func.args, newNode);

    return newNode;
}

static Symbol *DeclareLocal(Tiny_State *state, const char *name) {
    assert(state->currFunc);

    for (int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
        Symbol *sym = state->currFunc->func.locals[i];

        assert(sym->type == SYM_LOCAL);

        if (!sym->var.scopeEnded && strcmp(sym->name, name) == 0) {
            ReportError(state,
                        "Function '%s' has multiple locals in the same scope with "
                        "name '%s'.\n",
                        state->currFunc->name, name);
        }
    }

    Symbol *newNode = Symbol_create(SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = sb_count(state->currFunc->func.locals);
    newNode->var.scope = state->currScope;

    sb_push(&state->ctx, state->currFunc->func.locals, newNode);

    return newNode;
}

static Symbol *DeclareConst(Tiny_State *state, const char *name, Symbol *tag) {
    Symbol *sym = ReferenceVariable(state, name);

    if (sym && (sym->type == SYM_CONST || sym->type == SYM_LOCAL || sym->type == SYM_GLOBAL)) {
        ReportError(state,
                    "Attempted to define constant with the same name '%s' as "
                    "another value.\n",
                    name);
    }

    if (state->currFunc)
        fprintf(stderr,
                "Warning: Constant '%s' declared inside function bodies will still "
                "have global scope.\n",
                name);

    Symbol *newNode = Symbol_create(SYM_CONST, name, state);

    newNode->constant.tag = tag;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    return newNode;
}

static Symbol *DeclareFunction(Tiny_State *state, const char *name) {
    Symbol *newNode = Symbol_create(SYM_FUNCTION, name, state);

    newNode->func.index = state->numFunctions;
    newNode->func.args = NULL;
    newNode->func.locals = NULL;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    state->numFunctions += 1;

    return newNode;
}

static Symbol *ReferenceFunction(Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *node = state->globalSymbols[i];

        if ((node->type == SYM_FUNCTION || node->type == SYM_FOREIGN_FUNCTION) &&
            strcmp(node->name, name) == 0)
            return node;
    }

    return NULL;
}

static void BindFunction(Tiny_State *state, const char *name, Symbol **argTags, bool varargs,
                         Symbol *returnTag, Tiny_ForeignFunction func) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *node = state->globalSymbols[i];

        if (node->type == SYM_FOREIGN_FUNCTION && strcmp(node->name, name) == 0) {
            fprintf(stderr, "There is already a foreign function bound to name '%s'.", name);
            exit(1);
        }
    }

    Symbol *newNode = Symbol_create(SYM_FOREIGN_FUNCTION, name, state);

    newNode->foreignFunc.index = state->numForeignFunctions;

    newNode->foreignFunc.argTags = argTags;
    newNode->foreignFunc.varargs = varargs;

    newNode->foreignFunc.returnTag = returnTag;

    newNode->foreignFunc.callee = func;

    sb_push(&state->ctx, state->globalSymbols, newNode);

    state->numForeignFunctions += 1;
}

static Symbol *GetTagFromName(Tiny_State *state, const char *name, bool declareStruct);

void Tiny_RegisterType(Tiny_State *state, const char *name) {
    Symbol *s = GetTagFromName(state, name, false);

    if (s) return;

    s = Symbol_create(SYM_TAG_FOREIGN, name, state);

    sb_push(&state->ctx, state->globalSymbols, s);
}

static void ScanUntilDelim(const char **ps, char **buf, Tiny_Context *ctx) {
    const char *s = *ps;

    while (*s && *s != '(' && *s != ')' && *s != ',') {
        if (isspace(*s)) {
            s += 1;
            continue;
        }

        sb_push(ctx, *buf, *s++);
    }

    sb_push(ctx, *buf, 0);

    *ps = s;
}

void Tiny_BindFunction(Tiny_State *state, const char *sig, Tiny_ForeignFunction func) {
    char *name = NULL;

    ScanUntilDelim(&sig, &name, &state->ctx);

    if (!sig[0]) {
        // Just the name
        BindFunction(state, name, NULL, true, GetPrimTag(SYM_TAG_ANY), func);

        sb_free(&state->ctx, name);
        return;
    }

    sig += 1;

    Symbol **argTags = NULL;
    bool varargs = false;
    char *buf = NULL;

    while (*sig != ')' && !varargs) {
        ScanUntilDelim(&sig, &buf, &state->ctx);

        if (strcmp(buf, "...") == 0) {
            varargs = true;

            sb_free(&state->ctx, buf);
            buf = NULL;
            break;
        } else {
            Symbol *s = GetTagFromName(state, buf, false);

            assert(s);

            sb_push(&state->ctx, argTags, s);

            sb_free(&state->ctx, buf);
            buf = NULL;
        }

        if (*sig == ',') ++sig;
    }

    assert(*sig == ')');

    sig += 1;

    Symbol *returnTag = GetPrimTag(SYM_TAG_ANY);

    if (*sig == ':') {
        sig += 1;

        ScanUntilDelim(&sig, &buf, &state->ctx);

        returnTag = GetTagFromName(state, buf, false);
        assert(returnTag);

        sb_free(&state->ctx, buf);
    }

    BindFunction(state, name, argTags, varargs, returnTag, func);

    // BindFunction copies name
    sb_free(&state->ctx, name);
}

void Tiny_BindConstBool(Tiny_State *state, const char *name, bool b) {
    DeclareConst(state, name, GetPrimTag(SYM_TAG_BOOL))->constant.bValue = b;
}

void Tiny_BindConstInt(Tiny_State *state, const char *name, int i) {
    DeclareConst(state, name, GetPrimTag(SYM_TAG_INT))->constant.iValue = i;
}

void Tiny_BindConstFloat(Tiny_State *state, const char *name, float f) {
    DeclareConst(state, name, GetPrimTag(SYM_TAG_FLOAT))->constant.fValue = f;
}

void Tiny_BindConstString(Tiny_State *state, const char *name, const char *string) {
    DeclareConst(state, name, GetPrimTag(SYM_TAG_STR))->constant.sIndex =
        RegisterString(state, string);
}

static int ReadInteger(Tiny_StateThread *thread) {
    // Move PC up to the next 4 aligned thing
    thread->pc += (thread->pc % 4 == 0) ? 0 : (4 - thread->pc % 4);

    int val = *(int *)(&thread->state->program[thread->pc]);
    thread->pc += sizeof(int) / sizeof(Word);

    return val;
}

static float ReadFloat(Tiny_StateThread *thread) {
    // Move PC up to the next 4 aligned thing
    thread->pc += (thread->pc % 4 == 0) ? 0 : (4 - thread->pc % 4);

    float val = *(float *)(&thread->state->program[thread->pc]);
    thread->pc += sizeof(float) / sizeof(Word);

    return val;
}

static void DoPush(Tiny_StateThread *thread, Tiny_Value value) {
    thread->stack[thread->sp++] = value;
}

static inline Tiny_Value DoPop(Tiny_StateThread *thread) { return thread->stack[--thread->sp]; }

static void DoRead(Tiny_StateThread *thread) {
    char *buffer = TMalloc(&thread->ctx, 8);
    size_t bufferLength = 0;
    size_t bufferCapacity = 8;

    int c = getc(stdin);
    int i = 0;

    while (c != '\n') {
        if (bufferLength + 1 >= bufferCapacity) {
            bufferCapacity *= 2;
            buffer = TRealloc(&thread->ctx, buffer, bufferCapacity);
        }

        buffer[i++] = c;
        c = getc(stdin);
    }

    buffer[i] = '\0';

    Tiny_Object *obj = NewObject(thread, TINY_VAL_STRING);
    obj->string = buffer;

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    DoPush(thread, val);
}

static void DoPushIndir(Tiny_StateThread *thread, int nargs) {
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

static bool ExecuteCycle(Tiny_StateThread *thread) {
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

            Word n = thread->state->program[thread->pc++];

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

            thread->stack[thread->sp].type = TINY_VAL_INT;
            thread->stack[thread->sp].i = ReadInteger(thread);
            thread->sp += 1;
        } break;

        case TINY_OP_PUSH_0: {
            ++thread->pc;

            thread->stack[thread->sp].type = TINY_VAL_INT;
            thread->stack[thread->sp].i = 0;
            thread->sp += 1;
        } break;

        case TINY_OP_PUSH_1: {
            ++thread->pc;

            thread->stack[thread->sp].type = TINY_VAL_INT;
            thread->stack[thread->sp].i = 1;
            thread->sp += 1;
        } break;

        case TINY_OP_PUSH_CHAR: {
            ++thread->pc;

            thread->stack[thread->sp].type = TINY_VAL_INT;
            thread->stack[thread->sp].i = thread->state->program[thread->pc];
            thread->sp += 1;
            thread->pc += 1;
        } break;

        case TINY_OP_PUSH_FLOAT: {
            ++thread->pc;

            DoPush(thread, Tiny_NewFloat(ReadFloat(thread)));
        } break;

        case TINY_OP_PUSH_STRING: {
            ++thread->pc;

            int stringIndex = ReadInteger(thread);

            DoPush(thread, Tiny_NewConstString(thread->state->strings[stringIndex]));
        } break;

        case TINY_OP_PUSH_STRING_FF: {
            ++thread->pc;

            Word sIndex = thread->state->program[thread->pc++];
            DoPush(thread, Tiny_NewConstString(thread->state->strings[sIndex]));
        } break;

        case TINY_OP_PUSH_STRUCT: {
            ++thread->pc;

            Word nFields = thread->state->program[thread->pc++];
            assert(nFields > 0);

            Tiny_Object *obj = NewStructObject(thread, nFields);

            memcpy(obj->ostruct.fields, &thread->stack[thread->sp - nFields],
                   sizeof(Tiny_Value) * nFields);
            thread->sp -= nFields;

            thread->stack[thread->sp].type = TINY_VAL_STRUCT;
            thread->stack[thread->sp].obj = obj;

            thread->sp += 1;
        } break;

        case TINY_OP_STRUCT_GET: {
            ++thread->pc;

            Word i = thread->state->program[thread->pc++];

            assert(i >= 0 && i < thread->stack[thread->sp - 1].obj->ostruct.n);

            Tiny_Value val = thread->stack[thread->sp - 1].obj->ostruct.fields[i];
            thread->stack[thread->sp - 1] = val;
        } break;

        case TINY_OP_STRUCT_SET: {
            ++thread->pc;

            Word i = thread->state->program[thread->pc++];

            Tiny_Value vstruct = DoPop(thread);
            Tiny_Value val = DoPop(thread);

            assert(i >= 0 && i < vstruct.obj->ostruct.n);

            vstruct.obj->ostruct.fields[i] = val;
        } break;

#define BIN_OP(OP, operator)                                                 \
    case TINY_OP_##OP: {                                                     \
        Tiny_Value val2 = DoPop(thread);                                     \
        Tiny_Value val1 = DoPop(thread);                                     \
        if (val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_INT)        \
            DoPush(thread, Tiny_NewFloat(val1.f operator(float) val2.i));    \
        else if (val1.type == TINY_VAL_INT && val2.type == TINY_VAL_FLOAT)   \
            DoPush(thread, Tiny_NewFloat((float)val1.i operator val2.f));    \
        else if (val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_FLOAT) \
            DoPush(thread, Tiny_NewFloat(val1.f operator val2.f));           \
        else                                                                 \
            DoPush(thread, Tiny_NewInt(val1.i operator val2.i));             \
        ++thread->pc;                                                        \
    } break;

#define BIN_OP_INT(OP, operator)                                       \
    case TINY_OP_##OP: {                                               \
        Tiny_Value val2 = DoPop(thread);                               \
        Tiny_Value val1 = DoPop(thread);                               \
        DoPush(thread, Tiny_NewInt((int)val1.i operator(int) val2.i)); \
        ++thread->pc;                                                  \
    } break;

#define REL_OP(OP, operator)                                                 \
    case TINY_OP_##OP: {                                                     \
        Tiny_Value val2 = DoPop(thread);                                     \
        Tiny_Value val1 = DoPop(thread);                                     \
        if (val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_INT)        \
            DoPush(thread, Tiny_NewBool(val1.f operator(float) val2.i));     \
        else if (val1.type == TINY_VAL_INT && val2.type == TINY_VAL_FLOAT)   \
            DoPush(thread, Tiny_NewBool((float)val1.i operator val2.f));     \
        else if (val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_FLOAT) \
            DoPush(thread, Tiny_NewBool(val1.f operator val2.f));            \
        else                                                                 \
            DoPush(thread, Tiny_NewBool(val1.i operator val2.i));            \
        ++thread->pc;                                                        \
    } break;

            BIN_OP(ADD, +)
            BIN_OP(SUB, -)
            BIN_OP(MUL, *)
            BIN_OP(DIV, /)
            BIN_OP_INT(MOD, %)
            BIN_OP_INT(OR, |)
            BIN_OP_INT(AND, &)

            REL_OP(LT, <)
            REL_OP(LTE, <=)
            REL_OP(GT, >)
            REL_OP(GTE, >=)

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

            bool bothStrings = ((a.type == TINY_VAL_CONST_STRING && b.type == TINY_VAL_STRING) ||
                                (a.type == TINY_VAL_STRING && b.type == TINY_VAL_CONST_STRING));

            if (a.type != b.type && !bothStrings)
                DoPush(thread, Tiny_NewBool(false));
            else {
                if (a.type == TINY_VAL_NULL)
                    DoPush(thread, Tiny_NewBool(true));
                else if (a.type == TINY_VAL_BOOL)
                    DoPush(thread, Tiny_NewBool(a.boolean == b.boolean));
                else if (a.type == TINY_VAL_INT)
                    DoPush(thread, Tiny_NewBool(a.i == b.i));
                else if (a.type == TINY_VAL_FLOAT)
                    DoPush(thread, Tiny_NewBool(a.f == b.f));
                else if (a.type == TINY_VAL_STRING)
                    DoPush(thread, Tiny_NewBool(strcmp(a.obj->string, Tiny_ToString(b)) == 0));
                else if (a.type == TINY_VAL_CONST_STRING) {
                    if (b.type == TINY_VAL_CONST_STRING && a.cstr == b.cstr)
                        DoPush(thread, Tiny_NewBool(true));
                    else
                        DoPush(thread, Tiny_NewBool(strcmp(a.cstr, Tiny_ToString(b)) == 0));
                } else if (a.type == TINY_VAL_NATIVE)
                    DoPush(thread, Tiny_NewBool(a.obj->nat.addr == b.obj->nat.addr));
                else if (a.type == TINY_VAL_LIGHT_NATIVE)
                    DoPush(thread, Tiny_NewBool(a.addr == b.addr));
            }
        } break;

        case TINY_OP_LOG_NOT: {
            ++thread->pc;
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(!ExpectBool(a)));
        } break;

        case TINY_OP_LOG_AND: {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(ExpectBool(a) && ExpectBool(b)));
        } break;

        case TINY_OP_LOG_OR: {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(ExpectBool(a) || ExpectBool(b)));
        } break;

        case TINY_OP_PRINT: {
            Tiny_Value val = DoPop(thread);
            if (val.type == TINY_VAL_INT)
                printf("%d\n", val.i);
            else if (val.type == TINY_VAL_FLOAT)
                printf("%f\n", val.f);
            else if (val.obj->type == TINY_VAL_STRING)
                printf("%s\n", val.obj->string);
            else if (val.obj->type == TINY_VAL_CONST_STRING)
                printf("%s\n", val.cstr);
            else if (val.obj->type == TINY_VAL_NATIVE)
                printf("<native at %p>\n", val.obj->nat.addr);
            else if (val.obj->type == TINY_VAL_LIGHT_NATIVE)
                printf("<light native at %p>\n", val.obj->nat.addr);
            ++thread->pc;
        } break;

        case TINY_OP_SET: {
            ++thread->pc;
            int varIdx = ReadInteger(thread);
            thread->globalVars[varIdx] = DoPop(thread);
        } break;

        case TINY_OP_GET: {
            ++thread->pc;
            int varIdx = ReadInteger(thread);
            DoPush(thread, thread->globalVars[varIdx]);
        } break;

        case TINY_OP_READ: {
            DoRead(thread);
            ++thread->pc;
        } break;

        case TINY_OP_GOTO: {
            ++thread->pc;
            int newPc = ReadInteger(thread);
            thread->pc = newPc;
        } break;

        case TINY_OP_GOTOZ: {
            ++thread->pc;
            int newPc = ReadInteger(thread);

            Tiny_Value val = DoPop(thread);

            if (!ExpectBool(val)) thread->pc = newPc;
        } break;

        case TINY_OP_CALL: {
            ++thread->pc;
            Word nargs = thread->state->program[thread->pc++];
            int pcIdx = ReadInteger(thread);

            DoPushIndir(thread, nargs);
            thread->pc = state->functionPcs[pcIdx];
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

            Word nargs = thread->state->program[thread->pc++];
            int fIdx = ReadInteger(thread);

            // the state of the stack prior to the function arguments being pushed
            int prevSize = thread->sp - nargs;

            thread->retVal = state->foreignFunctions[fIdx](thread, &thread->stack[prevSize], nargs);

            // Resize the stack so that it has the arguments removed
            thread->sp = prevSize;
        } break;

        case TINY_OP_GETLOCAL: {
            ++thread->pc;
            int localIdx = ReadInteger(thread);
            DoPush(thread, thread->stack[thread->fp + localIdx]);
        } break;

        case TINY_OP_SETLOCAL: {
            ++thread->pc;
            Word localIdx = thread->state->program[thread->pc++];
            Tiny_Value val = DoPop(thread);
            thread->stack[thread->fp + localIdx] = val;
        } break;

        case TINY_OP_GET_RETVAL: {
            ++thread->pc;
            DoPush(thread, thread->retVal);
        } break;

        case TINY_OP_HALT: {
            thread->fileName = NULL;
            thread->lineNumber = -1;

            thread->pc = -1;
        } break;

        case TINY_OP_FILE: {
            ++thread->pc;
            int stringIndex = ReadInteger(thread);

            thread->fileName = thread->state->strings[stringIndex];
        } break;

        case TINY_OP_LINE: {
            ++thread->pc;
            int line = ReadInteger(thread);

            thread->lineNumber = line;
        } break;

        case TINY_OP_MISALIGNED_INSTRUCTION: {
            // TODO(Apaar): Proper runtime error
            assert(false);
        } break;

        default: {
            assert(false);
        } break;
    }

    // Only collect garbage in between iterations
    if (thread->numObjects >= thread->maxNumObjects) GarbageCollect(thread);

    return true;
}

typedef enum {
    EXP_ID,
    EXP_CALL,
    EXP_NULL,
    EXP_BOOL,
    EXP_CHAR,
    EXP_INT,
    EXP_FLOAT,
    EXP_STRING,
    EXP_BINARY,
    EXP_PAREN,
    EXP_BLOCK,
    EXP_PROC,
    EXP_IF,
    EXP_UNARY,
    EXP_RETURN,
    EXP_WHILE,
    EXP_FOR,
    EXP_DOT,
    EXP_CONSTRUCTOR,
    EXP_CAST,
    EXP_BREAK,
    EXP_CONTINUE
} ExprType;

typedef struct sExpr {
    ExprType type;

    Tiny_TokenPos pos;
    int lineNumber;

    Symbol *tag;

    union {
        bool boolean;

        int iValue;
        float fValue;
        int sIndex;

        struct {
            char *name;
            Symbol *sym;
        } id;

        struct {
            char *calleeName;
            struct sExpr **args;  // array
        } call;

        struct {
            struct sExpr *lhs;
            struct sExpr *rhs;
            int op;
        } binary;

        struct sExpr *paren;

        struct {
            int op;
            struct sExpr *exp;
        } unary;

        struct sExpr **block;  // array

        struct {
            Symbol *decl;
            struct sExpr *body;
        } proc;

        struct {
            struct sExpr *cond;
            struct sExpr *body;
            struct sExpr *alt;
        } ifx;

        struct {
            struct sExpr *cond;
            struct sExpr *body;
        } whilex;

        struct {
            struct sExpr *init;
            struct sExpr *cond;
            struct sExpr *step;
            struct sExpr *body;
        } forx;

        struct {
            struct sExpr *lhs;
            char *field;
        } dot;

        struct {
            Symbol *structTag;
            struct sExpr **args;
        } constructor;

        struct {
            struct sExpr *value;
            Symbol *tag;
        } cast;

        struct sExpr *retExpr;

        struct {
            // For a break, this index in the bytecode should be patched with the pc at the exit of
            // the loop. For a continue, this index should be patched with the PC before the
            // conditional.
            int patchLoc;
        } breakContinue;
    };
} Expr;

static Expr *Expr_create(ExprType type, Tiny_State *state) {
    Expr *exp = TMalloc(&state->ctx, sizeof(Expr));

    exp->pos = state->l.pos;
    exp->lineNumber = state->l.lineNumber;
    exp->type = type;

    exp->tag = NULL;

    return exp;
}

int CurTok;

static int GetNextToken(Tiny_State *state) {
    CurTok = Tiny_GetToken(&state->l);
    return CurTok;
}

static Expr *ParseExpr(Tiny_State *state);

static void ReportError(Tiny_State *state, const char *s, ...) {
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, state->l.pos, s, args);

    va_end(args);
    exit(1);
}

static void ReportErrorE(Tiny_State *state, const Expr *exp, const char *s, ...) {
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, exp->pos, s, args);

    va_end(args);
    exit(1);
}

static void ReportErrorS(Tiny_State *state, const Symbol *sym, const char *s, ...) {
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, sym->pos, s, args);

    va_end(args);
    exit(1);
}

static void ExpectToken(Tiny_State *state, int tok, const char *msg) {
    if (CurTok != tok) ReportError(state, msg);
}

static Symbol *GetStructTag(Tiny_State *state, const char *name) {
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        if (state->globalSymbols[i]->type == SYM_TAG_STRUCT &&
            strcmp(state->globalSymbols[i]->name, name) == 0) {
            return state->globalSymbols[i];
        }
    }

    return NULL;
}

static Symbol *DeclareStruct(Tiny_State *state, const char *name, bool search) {
    if (search) {
        Symbol *s = GetStructTag(state, name);
        if (s) return s;
    }

    Symbol *s = Symbol_create(SYM_TAG_STRUCT, name, state);

    s->sstruct.defined = false;
    s->sstruct.fields = NULL;

    sb_push(&state->ctx, state->globalSymbols, s);

    return s;
}

static Symbol *GetTagFromName(Tiny_State *state, const char *name, bool declareStruct) {
    if (strcmp(name, "void") == 0)
        return GetPrimTag(SYM_TAG_VOID);
    else if (strcmp(name, "bool") == 0)
        return GetPrimTag(SYM_TAG_BOOL);
    else if (strcmp(name, "int") == 0)
        return GetPrimTag(SYM_TAG_INT);
    else if (strcmp(name, "float") == 0)
        return GetPrimTag(SYM_TAG_FLOAT);
    else if (strcmp(name, "str") == 0)
        return GetPrimTag(SYM_TAG_STR);
    else if (strcmp(name, "any") == 0)
        return GetPrimTag(SYM_TAG_ANY);
    else {
        for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
            Symbol *s = state->globalSymbols[i];

            if ((s->type == SYM_TAG_FOREIGN || s->type == SYM_TAG_STRUCT) &&
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

static Symbol *GetFieldTag(Symbol *s, const char *name, int *index) {
    assert(s->type == SYM_TAG_STRUCT);
    assert(s->sstruct.defined);

    for (int i = 0; i < sb_count(s->sstruct.fields); ++i) {
        Symbol *f = s->sstruct.fields[i];

        if (strcmp(f->name, name) == 0) {
            if (index) *index = i;
            return f->fieldTag;
        }
    }

    if (index) *index = -1;

    return NULL;
}

static Symbol *ParseType(Tiny_State *state) {
    ExpectToken(state, TINY_TOK_IDENT, "Expected identifier for typename.");

    Symbol *s = GetTagFromName(state, state->l.lexeme, true);

    if (!s) {
        ReportError(state, "%s doesn't name a type.", state->l.lexeme);
    }

    GetNextToken(state);

    return s;
}

static Expr *ParseStatement(Tiny_State *state);

static Expr *ParseIf(Tiny_State *state) {
    Expr *exp = Expr_create(EXP_IF, state);

    GetNextToken(state);

    exp->ifx.cond = ParseExpr(state);
    exp->ifx.body = ParseStatement(state);

    if (CurTok == TINY_TOK_ELSE) {
        GetNextToken(state);
        exp->ifx.alt = ParseStatement(state);
    } else
        exp->ifx.alt = NULL;

    return exp;
}

static Expr *ParseBlock(Tiny_State *state) {
    assert(CurTok == TINY_TOK_OPENCURLY);

    Expr *exp = Expr_create(EXP_BLOCK, state);

    exp->block = NULL;

    GetNextToken(state);

    OpenScope(state);

    while (CurTok != TINY_TOK_CLOSECURLY) {
        Expr *e = ParseStatement(state);
        assert(e);

        sb_push(&state->ctx, exp->block, e);
    }

    GetNextToken(state);

    CloseScope(state);

    return exp;
}

static Expr *ParseFunc(Tiny_State *state) {
    assert(CurTok == TINY_TOK_FUNC);

    if (state->currFunc) {
        ReportError(state, "Attempted to define function inside of function '%s'.",
                    state->currFunc->name);
    }

    Expr *exp = Expr_create(EXP_PROC, state);

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_IDENT, "Function name must be identifier!");

    exp->proc.decl = DeclareFunction(state, state->l.lexeme);
    state->currFunc = exp->proc.decl;

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_OPENPAREN, "Expected '(' after function name");
    GetNextToken(state);

    typedef struct {
        char *name;
        Symbol *tag;
    } Arg;

    Arg *args = NULL;  // array

    while (CurTok != TINY_TOK_CLOSEPAREN) {
        ExpectToken(state, TINY_TOK_IDENT, "Expected identifier in function parameter list");

        Arg arg;

        arg.name = CloneString(&state->ctx, state->l.lexeme);
        GetNextToken(state);

        if (CurTok != TINY_TOK_COLON) {
            ReportError(state, "Expected ':' after %s", arg.name);
        }

        GetNextToken(state);

        arg.tag = ParseType(state);

        sb_push(&state->ctx, args, arg);

        if (CurTok != TINY_TOK_CLOSEPAREN && CurTok != TINY_TOK_COMMA) {
            ReportError(state,
                        "Expected ')' or ',' after parameter name in function "
                        "parameter list.");
        }

        if (CurTok == TINY_TOK_COMMA) GetNextToken(state);
    }

    for (int i = 0; i < sb_count(args); ++i) {
        DeclareArgument(state, args[i].name, args[i].tag, sb_count(args));
        TFree(&state->ctx, args[i].name);
    }

    sb_free(&state->ctx, args);

    GetNextToken(state);

    if (CurTok != TINY_TOK_COLON) {
        exp->proc.decl->func.returnTag = GetPrimTag(SYM_TAG_VOID);
    } else {
        GetNextToken(state);
        exp->proc.decl->func.returnTag = ParseType(state);
    }

    OpenScope(state);

    exp->proc.body = ParseStatement(state);

    CloseScope(state);

    state->currFunc = NULL;

    return exp;
}

static Symbol *ParseStruct(Tiny_State *state) {
    if (state->currFunc) {
        ReportError(state, "Attempted to declare struct inside func %s. Can't do that bruh.",
                    state->currFunc->name);
    }

    Tiny_TokenPos pos = state->l.pos;

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_IDENT, "Expected identifier after 'struct'.");

    Symbol *s = DeclareStruct(state, state->l.lexeme, true);

    if (s->sstruct.defined) {
        ReportError(state, "Attempted to define struct %s multiple times.", state->l.lexeme);
    }

    s->pos = pos;
    s->sstruct.defined = true;

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_OPENCURLY, "Expected '{' after struct name.");

    GetNextToken(state);

    while (CurTok != TINY_TOK_CLOSECURLY) {
        ExpectToken(state, TINY_TOK_IDENT, "Expected identifier in struct fields.");

        int count = sb_count(s->sstruct.fields);

        if (count >= UCHAR_MAX) {
            ReportError(state, "Too many fields in struct.");
        }

        for (int i = 0; i < count; ++i) {
            if (strcmp(s->sstruct.fields[i]->name, state->l.lexeme) == 0) {
                ReportError(state, "Declared multiple fields with the same name %s.",
                            state->l.lexeme);
            }
        }

        Symbol *f = Symbol_create(SYM_FIELD, state->l.lexeme, state);

        GetNextToken(state);

        ExpectToken(state, TINY_TOK_COLON, "Expected ':' after field name.");

        GetNextToken(state);

        f->fieldTag = ParseType(state);

        sb_push(&state->ctx, s->sstruct.fields, f);
    }

    GetNextToken(state);

    if (!s->sstruct.fields) {
        ReportError(state, "Struct must have at least one field.\n");
    }

    return s;
}

static Expr *ParseCall(Tiny_State *state, char *ident) {
    assert(CurTok == TINY_TOK_OPENPAREN);

    Expr *exp = Expr_create(EXP_CALL, state);

    exp->call.args = NULL;

    GetNextToken(state);

    while (CurTok != TINY_TOK_CLOSEPAREN) {
        sb_push(&state->ctx, exp->call.args, ParseExpr(state));

        if (CurTok == TINY_TOK_COMMA)
            GetNextToken(state);
        else if (CurTok != TINY_TOK_CLOSEPAREN) {
            ReportError(state, "Expected ')' after call.");
        }
    }

    exp->call.calleeName = ident;

    GetNextToken(state);
    return exp;
}

static Expr *ParseFactor(Tiny_State *state) {
    switch (CurTok) {
        case TINY_TOK_NULL: {
            Expr *exp = Expr_create(EXP_NULL, state);

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_BOOL: {
            Expr *exp = Expr_create(EXP_BOOL, state);

            exp->boolean = state->l.bValue;

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_IDENT: {
            char *ident = CloneString(&state->ctx, state->l.lexeme);
            GetNextToken(state);

            if (CurTok == TINY_TOK_OPENPAREN) return ParseCall(state, ident);

            Expr *exp = Expr_create(EXP_ID, state);

            exp->id.sym = ReferenceVariable(state, ident);
            exp->id.name = ident;

            while (CurTok == TINY_TOK_DOT) {
                Expr *e = Expr_create(EXP_DOT, state);

                GetNextToken(state);

                ExpectToken(state, TINY_TOK_IDENT, "Expected identifier after '.'");

                e->dot.lhs = exp;
                e->dot.field = CloneString(&state->ctx, state->l.lexeme);

                GetNextToken(state);

                exp = e;
            }

            return exp;
        } break;

        case TINY_TOK_MINUS:
        case TINY_TOK_BANG: {
            int op = CurTok;
            GetNextToken(state);
            Expr *exp = Expr_create(EXP_UNARY, state);
            exp->unary.op = op;
            exp->unary.exp = ParseFactor(state);

            return exp;
        } break;

        case TINY_TOK_CHAR: {
            Expr *exp = Expr_create(EXP_CHAR, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_INT: {
            Expr *exp = Expr_create(EXP_INT, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_FLOAT: {
            Expr *exp = Expr_create(EXP_FLOAT, state);
            exp->fValue = state->l.fValue;
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_STRING: {
            Expr *exp = Expr_create(EXP_STRING, state);
            exp->sIndex = RegisterString(state, state->l.lexeme);
            GetNextToken(state);
            return exp;
        } break;

        case TINY_TOK_OPENPAREN: {
            GetNextToken(state);
            Expr *inner = ParseExpr(state);

            ExpectToken(state, TINY_TOK_CLOSEPAREN, "Expected matching ')' after previous '('");
            GetNextToken(state);

            Expr *exp = Expr_create(EXP_PAREN, state);
            exp->paren = inner;
            return exp;
        } break;

        case TINY_TOK_NEW: {
            Expr *exp = Expr_create(EXP_CONSTRUCTOR, state);

            GetNextToken(state);

            Symbol *tag = DeclareStruct(state, state->l.lexeme, true);

            GetNextToken(state);

            exp->constructor.structTag = tag;
            exp->constructor.args = NULL;

            ExpectToken(state, TINY_TOK_OPENCURLY, "Expected '{' after struct name");

            GetNextToken(state);

            while (CurTok != TINY_TOK_CLOSECURLY) {
                Expr *e = ParseExpr(state);

                sb_push(&state->ctx, exp->constructor.args, e);

                if (CurTok != TINY_TOK_CLOSECURLY && CurTok != TINY_TOK_COMMA) {
                    ReportError(state, "Expected '}' or ',' in constructor arg list.");
                }

                if (CurTok == TINY_TOK_COMMA) GetNextToken(state);
            }

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_CAST: {
            Expr *exp = Expr_create(EXP_CAST, state);

            GetNextToken(state);

            ExpectToken(state, TINY_TOK_OPENPAREN, "Expected '(' after cast");

            GetNextToken(state);

            exp->cast.value = ParseExpr(state);

            ExpectToken(state, TINY_TOK_COMMA, "Expected ',' after cast value");

            GetNextToken(state);

            exp->cast.tag = ParseType(state);

            ExpectToken(state, TINY_TOK_CLOSEPAREN,
                        "Expected ')' to match previous '(' after cast.");

            GetNextToken(state);

            return exp;
        } break;

        default:
            break;
    }

    ReportError(state, "Unexpected token '%s'\n", state->l.lexeme);
    return NULL;
}

static int GetTokenPrec() {
    int prec = -1;
    switch (CurTok) {
        case TINY_TOK_STAR:
        case TINY_TOK_SLASH:
        case TINY_TOK_PERCENT:
        case TINY_TOK_AND:
        case TINY_TOK_OR:
            prec = 5;
            break;

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

static Expr *ParseBinRhs(Tiny_State *state, int exprPrec, Expr *lhs) {
    while (true) {
        int prec = GetTokenPrec();

        if (prec < exprPrec) return lhs;

        int binOp = CurTok;

        GetNextToken(state);

        Expr *rhs = ParseFactor(state);
        int nextPrec = GetTokenPrec();

        if (prec < nextPrec) rhs = ParseBinRhs(state, prec + 1, rhs);

        Expr *newLhs = Expr_create(EXP_BINARY, state);

        newLhs->binary.lhs = lhs;
        newLhs->binary.rhs = rhs;
        newLhs->binary.op = binOp;

        lhs = newLhs;
    }
}

static Expr *ParseExpr(Tiny_State *state) {
    Expr *factor = ParseFactor(state);
    return ParseBinRhs(state, 0, factor);
}

static Expr *ParseStatement(Tiny_State *state) {
    switch (CurTok) {
        case TINY_TOK_OPENCURLY:
            return ParseBlock(state);

        case TINY_TOK_IDENT: {
            char *ident = CloneString(&state->ctx, state->l.lexeme);
            GetNextToken(state);

            if (CurTok == TINY_TOK_OPENPAREN) return ParseCall(state, ident);

            Expr *lhs = Expr_create(EXP_ID, state);

            lhs->id.sym = ReferenceVariable(state, ident);
            lhs->id.name = ident;

            while (CurTok == TINY_TOK_DOT) {
                Expr *e = Expr_create(EXP_DOT, state);

                GetNextToken(state);

                ExpectToken(state, TINY_TOK_IDENT, "Expected identifier after '.'");

                e->dot.lhs = lhs;
                e->dot.field = CloneString(&state->ctx, state->l.lexeme);

                GetNextToken(state);

                lhs = e;
            }

            int op = CurTok;

            if (CurTok == TINY_TOK_DECLARE || CurTok == TINY_TOK_COLON) {
                if (lhs->type != EXP_ID) {
                    ReportError(state, "Left hand side of declaration must be identifier.");
                }

                if (state->currFunc) {
                    lhs->id.sym = DeclareLocal(state, ident);
                } else {
                    lhs->id.sym = DeclareGlobalVar(state, ident);
                }

                if (CurTok == TINY_TOK_COLON) {
                    GetNextToken(state);
                    lhs->id.sym->var.tag = ParseType(state);

                    ExpectToken(state, TINY_TOK_EQUAL, "Expected '=' after typename.");

                    op = TINY_TOK_EQUAL;
                }
            }

            // If the precedence is >= 0 then it's an expression operator
            if (GetTokenPrec(op) >= 0) {
                ReportError(state, "Expected assignment statement.");
            }

            GetNextToken(state);

            Expr *rhs = ParseExpr(state);

            if (op == TINY_TOK_DECLARECONST) {
                if (lhs->type != EXP_ID) {
                    ReportError(state, "Left hand side of declaration must be identifier.");
                }

                if (rhs->type == EXP_BOOL) {
                    DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_BOOL))->constant.bValue =
                        rhs->boolean;
                } else if (rhs->type == EXP_CHAR) {
                    DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_INT))->constant.iValue =
                        rhs->iValue;
                } else if (rhs->type == EXP_INT) {
                    DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_INT))->constant.iValue =
                        rhs->iValue;
                } else if (rhs->type == EXP_FLOAT) {
                    DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_FLOAT))->constant.fValue =
                        rhs->fValue;
                } else if (rhs->type == EXP_STRING) {
                    DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_STR))->constant.sIndex =
                        rhs->sIndex;
                } else {
                    ReportError(state, "Expected number or string to be bound to constant '%s'.",
                                lhs->id.name);
                }
            }

            Expr *bin = Expr_create(EXP_BINARY, state);

            bin->binary.lhs = lhs;
            bin->binary.rhs = rhs;
            bin->binary.op = op;

            return bin;
        } break;

        case TINY_TOK_FUNC:
            return ParseFunc(state);

        case TINY_TOK_IF:
            return ParseIf(state);

        case TINY_TOK_WHILE: {
            GetNextToken(state);
            Expr *exp = Expr_create(EXP_WHILE, state);

            exp->whilex.cond = ParseExpr(state);

            OpenScope(state);

            exp->whilex.body = ParseStatement(state);

            CloseScope(state);

            return exp;
        } break;

        case TINY_TOK_FOR: {
            GetNextToken(state);

            Expr *exp = Expr_create(EXP_FOR, state);

            // Every local declared after this is scoped to the for
            OpenScope(state);

            exp->forx.init = ParseStatement(state);

            ExpectToken(state, TINY_TOK_SEMI, "Expected ';' after for initializer.");

            GetNextToken(state);

            exp->forx.cond = ParseExpr(state);

            ExpectToken(state, TINY_TOK_SEMI, "Expected ';' after for condition.");

            GetNextToken(state);

            exp->forx.step = ParseStatement(state);

            exp->forx.body = ParseStatement(state);

            CloseScope(state);

            return exp;
        } break;

        case TINY_TOK_RETURN: {
            if (!state->currFunc) {
                ReportError(state,
                            "Attempted to return from outside a function. Why? Why would "
                            "you do that? Why would you do any of that?");
            }

            GetNextToken(state);
            Expr *exp = Expr_create(EXP_RETURN, state);
            if (CurTok == TINY_TOK_SEMI) {
                GetNextToken(state);
                exp->retExpr = NULL;
                return exp;
            }

            if (state->currFunc->func.returnTag->type == SYM_TAG_VOID) {
                ReportError(state,
                            "Attempted to return value from function which is "
                            "supposed to return nothing (void).");
            }

            exp->retExpr = ParseExpr(state);
            return exp;
        } break;

        // TODO(Apaar): Labeled break/continue
        case TINY_TOK_BREAK: {
            GetNextToken(state);
            Expr *exp = Expr_create(EXP_BREAK, state);

            // Set to -1 to make sure we don't get into trouble
            exp->breakContinue.patchLoc = -1;

            return exp;
        } break;

        case TINY_TOK_CONTINUE: {
            GetNextToken(state);
            Expr *exp = Expr_create(EXP_CONTINUE, state);

            // Set to -1 to make sure we don't get into trouble
            exp->breakContinue.patchLoc = -1;

            return exp;
        } break;
    }

    ReportError(state, "Unexpected token '%s'.", state->l.lexeme);
    return NULL;
}

static Expr **ParseProgram(Tiny_State *state) {
    GetNextToken(state);

    if (CurTok != TINY_TOK_EOF) {
        Expr **arr = NULL;

        while (CurTok != TINY_TOK_EOF) {
            if (CurTok == TINY_TOK_STRUCT) {
                ParseStruct(state);
            } else {
                Expr *stmt = ParseStatement(state);
                sb_push(&state->ctx, arr, stmt);
            }
        }

        return arr;
    }

    return NULL;
}

static const char *GetTagName(const Symbol *tag) {
    assert(tag);
    return tag->name;
}

// Checks if types can be narrowed/widened to be equal
static bool CompareTags(const Symbol *a, const Symbol *b) {
    if (a->type == SYM_TAG_VOID) {
        return b->type == SYM_TAG_VOID;
    }

    // Can convert *to* any implicitly
    if (b->type == SYM_TAG_ANY) {
        return true;
    }

    if (a->type == b->type) {
        return strcmp(a->name, b->name) == 0;
    }

    return false;
}

static void ResolveTypes(Tiny_State *state, Expr *exp) {
    if (exp->tag) return;

    switch (exp->type) {
        case EXP_NULL:
            exp->tag = GetPrimTag(SYM_TAG_ANY);
            break;
        case EXP_BOOL:
            exp->tag = GetPrimTag(SYM_TAG_BOOL);
            break;
        case EXP_CHAR:
            exp->tag = GetPrimTag(SYM_TAG_INT);
            break;
        case EXP_INT:
            exp->tag = GetPrimTag(SYM_TAG_INT);
            break;
        case EXP_FLOAT:
            exp->tag = GetPrimTag(SYM_TAG_FLOAT);
            break;
        case EXP_STRING:
            exp->tag = GetPrimTag(SYM_TAG_STR);
            break;

        case EXP_ID: {
            if (!exp->id.sym) {
                ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n", exp->id.name);
            }

            assert(exp->id.sym->type == SYM_GLOBAL || exp->id.sym->type == SYM_LOCAL ||
                   exp->id.sym->type == SYM_CONST);

            if (exp->id.sym->type != SYM_CONST) {
                assert(exp->id.sym->var.tag);

                exp->tag = exp->id.sym->var.tag;
            } else {
                exp->tag = exp->id.sym->constant.tag;
            }
        } break;

        case EXP_CALL: {
            Symbol *func = ReferenceFunction(state, exp->call.calleeName);

            if (!func) {
                ReportErrorE(state, exp, "Calling undeclared function '%s'.\n",
                             exp->call.calleeName);
            }

            if (func->type == SYM_FOREIGN_FUNCTION) {
                for (int i = 0; i < sb_count(exp->call.args); ++i) {
                    ResolveTypes(state, exp->call.args[i]);

                    if (i >= sb_count(func->foreignFunc.argTags)) {
                        if (!func->foreignFunc.varargs) {
                            ReportErrorE(state, exp->call.args[i],
                                         "Too many arguments to function '%s' (%d expected).\n",
                                         exp->call.calleeName, sb_count(func->foreignFunc.argTags));
                        }

                        // We don't break because we want all types to be resolved
                    } else {
                        const Symbol *argTag = func->foreignFunc.argTags[i];

                        if (!CompareTags(exp->call.args[i]->tag, argTag)) {
                            ReportErrorE(
                                state, exp->call.args[i],
                                "Argument %i is supposed to be a %s but you supplied a %s\n", i + 1,
                                GetTagName(argTag), GetTagName(exp->call.args[i]->tag));
                        }
                    }
                }

                exp->tag = func->foreignFunc.returnTag;
            } else {
                if (sb_count(exp->call.args) != sb_count(func->func.args)) {
                    ReportErrorE(state, exp, "%s expects %d arguments but you supplied %d.\n",
                                 exp->call.calleeName, sb_count(func->func.args),
                                 sb_count(exp->call.args));
                }

                for (int i = 0; i < sb_count(exp->call.args); ++i) {
                    ResolveTypes(state, exp->call.args[i]);

                    const Symbol *argSym = func->func.args[i];

                    if (!CompareTags(exp->call.args[i]->tag, argSym->var.tag)) {
                        ReportErrorE(state, exp->call.args[i],
                                     "Argument %i is supposed to be a %s but you supplied a %s\n",
                                     i + 1, GetTagName(argSym->var.tag),
                                     GetTagName(exp->call.args[i]->tag));
                    }
                }

                exp->tag = func->func.returnTag;
            }
        } break;

        case EXP_PAREN: {
            ResolveTypes(state, exp->paren);

            exp->tag = exp->paren->tag;
        } break;

        case EXP_BINARY: {
            switch (exp->binary.op) {
                case TINY_TOK_PLUS:
                case TINY_TOK_MINUS:
                case TINY_TOK_STAR:
                case TINY_TOK_SLASH: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
                    bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

                    bool fLhs =
                        !iLhs && CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_FLOAT));
                    bool fRhs =
                        !iRhs && CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_FLOAT));

                    if ((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary op must be ints or "
                                     "floats, but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag((iLhs && iRhs) ? SYM_TAG_INT : SYM_TAG_FLOAT);
                } break;

                case TINY_TOK_AND:
                case TINY_TOK_OR:
                case TINY_TOK_PERCENT: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
                    bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

                    if (!(iLhs && iRhs)) {
                        ReportErrorE(
                            state, exp,
                            "Both sides of binary op must be ints, but they're %s and %s\n",
                            GetTagName(exp->binary.lhs->tag), GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_INT);
                } break;

                case TINY_TOK_LOG_AND:
                case TINY_TOK_LOG_OR: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (!CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_BOOL)) ||
                        !CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_BOOL))) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary and/or must be bools, "
                                     "but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

                case TINY_TOK_GT:
                case TINY_TOK_LT:
                case TINY_TOK_LTE:
                case TINY_TOK_GTE: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
                    bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

                    bool fLhs =
                        !iLhs && CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_FLOAT));
                    bool fRhs =
                        !iRhs && CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_FLOAT));

                    if ((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp,
                                     "Left and right hand side of binary comparison must be "
                                     "ints or floats, but they're %s and %s",
                                     GetTagName(exp->binary.lhs->tag),
                                     GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

                case TINY_TOK_EQUALS:
                case TINY_TOK_NOTEQUALS: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (exp->binary.lhs->tag->type == SYM_TAG_VOID ||
                        exp->binary.rhs->tag->type == SYM_TAG_VOID) {
                        ReportErrorE(
                            state, exp,
                            "Attempted to check for equality with void. This is not allowed.");
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

                case TINY_TOK_DECLARE: {
                    assert(exp->binary.lhs->type == EXP_ID);

                    assert(exp->binary.lhs->id.sym);

                    ResolveTypes(state, exp->binary.rhs);

                    if (exp->binary.rhs->tag->type == SYM_TAG_VOID) {
                        ReportErrorE(state, exp,
                                     "Attempted to initialize variable with void expression. "
                                     "Don't do that.");
                    }

                    exp->binary.lhs->id.sym->var.tag = exp->binary.rhs->tag;

                    exp->tag = GetPrimTag(SYM_TAG_VOID);
                } break;

                case TINY_TOK_EQUAL: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if (!CompareTags(exp->binary.lhs->tag, exp->binary.rhs->tag)) {
                        ReportErrorE(state, exp, "Attempted to assign a %s to a %s",
                                     GetTagName(exp->binary.rhs->tag),
                                     GetTagName(exp->binary.lhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_VOID);
                } break;

                default: {
                    ResolveTypes(state, exp->binary.rhs);
                    exp->tag = GetPrimTag(SYM_TAG_VOID);
                } break;
            }
        } break;

        case EXP_UNARY: {
            ResolveTypes(state, exp->unary.exp);

            switch (exp->unary.op) {
                case TINY_TOK_MINUS: {
                    bool i = CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_INT));
                    bool f = !i && CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_FLOAT));

                    if (!(i || f)) {
                        ReportErrorE(state, exp, "Attempted to apply unary '-' to a %s.",
                                     GetTagName(exp->unary.exp->tag));
                    }

                    exp->tag = i ? GetPrimTag(SYM_TAG_INT) : GetPrimTag(SYM_TAG_FLOAT);
                } break;

                case TINY_TOK_BANG: {
                    if (!CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_BOOL))) {
                        ReportErrorE(state, exp, "Attempted to apply unary 'not' to a %s.",
                                     GetTagName(exp->unary.exp->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

                default:
                    assert(0);
                    break;
            }
        } break;

        case EXP_BLOCK: {
            for (int i = 0; i < sb_count(exp->block); ++i) {
                ResolveTypes(state, exp->block[i]);
            }

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_PROC: {
            ResolveTypes(state, exp->proc.body);

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_IF: {
            ResolveTypes(state, exp->ifx.cond);

            if (!CompareTags(exp->ifx.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "If condition is supposed to be a bool but its a %s",
                             GetTagName(exp->ifx.cond->tag));
            }

            ResolveTypes(state, exp->ifx.body);

            if (exp->ifx.alt) {
                ResolveTypes(state, exp->ifx.alt);
            }

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_RETURN: {
            if (exp->retExpr) {
                ResolveTypes(state, exp->retExpr);
            }

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_WHILE: {
            ResolveTypes(state, exp->whilex.cond);

            if (!CompareTags(exp->whilex.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "While condition is supposed to be a bool but its a %s",
                             GetTagName(exp->whilex.cond->tag));
            }

            ResolveTypes(state, exp->whilex.body);

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_FOR: {
            ResolveTypes(state, exp->forx.init);
            ResolveTypes(state, exp->forx.cond);

            if (!CompareTags(exp->forx.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "For condition is supposed to be a bool but its a %s",
                             GetTagName(exp->forx.cond->tag));
            }

            ResolveTypes(state, exp->forx.step);
            ResolveTypes(state, exp->forx.body);

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_DOT: {
            ResolveTypes(state, exp->dot.lhs);

            if (exp->dot.lhs->tag->type != SYM_TAG_STRUCT) {
                ReportErrorE(state, exp, "Cannot use '.' on a %s", GetTagName(exp->dot.lhs->tag));
            }

            exp->tag = GetFieldTag(exp->dot.lhs->tag, exp->dot.field, NULL);

            if (!exp->tag) {
                ReportErrorE(state, exp, "Struct %s doesn't have a field named %s",
                             exp->dot.lhs->tag->name, exp->dot.field);
            }
        } break;

        case EXP_CONSTRUCTOR: {
            assert(exp->constructor.structTag->sstruct.defined);
            assert(sb_count(exp->constructor.args) <= UCHAR_MAX);

            int meCount = sb_count(exp->constructor.args);
            int tagCount = sb_count(exp->constructor.structTag->sstruct.fields);

            if (meCount != tagCount) {
                ReportErrorE(state, exp,
                             "struct %s constructor expects %d args but you supplied %d.",
                             exp->constructor.structTag->name, tagCount, meCount);
            }

            for (int i = 0; i < sb_count(exp->constructor.args); ++i) {
                ResolveTypes(state, exp->constructor.args[i]);

                Symbol *provided = exp->constructor.args[i]->tag;
                Symbol *expected = exp->constructor.structTag->sstruct.fields[i]->fieldTag;

                if (!CompareTags(provided, expected)) {
                    ReportErrorE(state, exp->constructor.args[i],
                                 "Argument %d to constructor is supposed to be a %s but "
                                 "you supplied a %s",
                                 i + 1, GetTagName(expected), GetTagName(provided));
                }
            }

            exp->tag = exp->constructor.structTag;
        } break;

        case EXP_CAST: {
            assert(exp->cast.value);
            assert(exp->cast.tag);

            ResolveTypes(state, exp->cast.value);

            // TODO(Apaar): Allow casting of int to float etc

            // Only allow casting "any" values for now
            if (exp->cast.value->tag != GetPrimTag(SYM_TAG_ANY)) {
                ReportErrorE(state, exp->cast.value, "Attempted to cast a %s; only any is allowed.",
                             GetTagName(exp->cast.value->tag));
            }

            exp->tag = exp->cast.tag;
        } break;
    }
}

static void CompileProgram(Tiny_State *state, Expr **program);

static void GeneratePushInt(Tiny_State *state, int iValue) {
    if (iValue == 0) {
        GenerateCode(state, TINY_OP_PUSH_0);
    } else if (iValue == 1) {
        GenerateCode(state, TINY_OP_PUSH_1);
    } else {
        // TODO(Apaar): Add small integer optimization
        GenerateCode(state, TINY_OP_PUSH_INT);
        GenerateInt(state, iValue);
    }
}

static void GeneratePushFloat(Tiny_State *state, float fValue) {
    GenerateCode(state, TINY_OP_PUSH_FLOAT);

    union {
        float f;
        int i;
    } value;

    value.f = fValue;

    // We reinterpret the bits of the float as an int
    GenerateInt(state, value.i);
}

static void GeneratePushString(Tiny_State *state, int sIndex) {
    if (sIndex <= 0xff) {
        GenerateCode(state, TINY_OP_PUSH_STRING_FF);
        GenerateCode(state, (Word)sIndex);
    } else {
        GenerateCode(state, TINY_OP_PUSH_STRING);
        GenerateInt(state, sIndex);
    }
}

static void CompileExpr(Tiny_State *state, Expr *exp);

static void CompileGetIdOrDot(Tiny_State *state, Expr *exp) {
    if (exp->type == EXP_ID) {
        if (!exp->id.sym)
            ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n", exp->id.name);

        assert(exp->id.sym->type == SYM_GLOBAL || exp->id.sym->type == SYM_LOCAL ||
               exp->id.sym->type == SYM_CONST);

        if (exp->id.sym->type != SYM_CONST) {
            if (exp->id.sym->type == SYM_GLOBAL)
                GenerateCode(state, TINY_OP_GET);
            else if (exp->id.sym->type == SYM_LOCAL)
                GenerateCode(state, TINY_OP_GETLOCAL);

            GenerateInt(state, exp->id.sym->var.index);
        } else {
            if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_STR)) {
                GeneratePushString(state, exp->id.sym->constant.sIndex);
            } else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_BOOL)) {
                GenerateCode(state,
                             exp->id.sym->constant.bValue ? TINY_OP_PUSH_TRUE : TINY_OP_PUSH_FALSE);
            } else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_INT)) {
                GeneratePushInt(state, exp->id.sym->constant.iValue);
            } else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_FLOAT)) {
                GeneratePushFloat(state, exp->id.sym->constant.fValue);
            } else {
                assert(0);
            }
        }
    } else {
        assert(exp->type == EXP_DOT);

        assert(exp->dot.lhs);
        assert(exp->dot.field);
        assert(exp->dot.lhs->tag->type == SYM_TAG_STRUCT);

        int idx;

        GetFieldTag(exp->dot.lhs->tag, exp->dot.field, &idx);

        assert(idx >= 0 && idx <= UCHAR_MAX);

        CompileExpr(state, exp->dot.lhs);

        GenerateCode(state, TINY_OP_STRUCT_GET);
        GenerateCode(state, (Word)idx);
    }
}

static void CompileCall(Tiny_State *state, Expr *exp) {
    assert(exp->type == EXP_CALL);

    if (sb_count(exp->call.args) > UCHAR_MAX) {
        ReportErrorE(state, exp, "Exceeded maximum number of arguments (%d).", UCHAR_MAX);
    }

    for (int i = 0; i < sb_count(exp->call.args); ++i) CompileExpr(state, exp->call.args[i]);

    Symbol *sym = ReferenceFunction(state, exp->call.calleeName);
    if (!sym) {
        ReportErrorE(state, exp, "Attempted to call undefined function '%s'.\n",
                     exp->call.calleeName);
    }

    if (sym->type == SYM_FOREIGN_FUNCTION) {
        GenerateCode(state, TINY_OP_CALLF);

        int nargs = sb_count(exp->call.args);
        int fNargs = sb_count(sym->foreignFunc.argTags);

        if (!(sym->foreignFunc.varargs && nargs >= fNargs) && fNargs != nargs) {
            ReportErrorE(state, exp, "Function '%s' expects %s%d args but you supplied %d.\n",
                         exp->call.calleeName, sym->foreignFunc.varargs ? "at least " : "", fNargs,
                         nargs);
        }

        GenerateCode(state, (Word)sb_count(exp->call.args));
        GenerateInt(state, sym->foreignFunc.index);
    } else {
        GenerateCode(state, TINY_OP_CALL);
        GenerateCode(state, (Word)sb_count(exp->call.args));
        GenerateInt(state, sym->func.index);
    }
}

static void CompileExpr(Tiny_State *state, Expr *exp) {
    switch (exp->type) {
        case EXP_NULL: {
            GenerateCode(state, TINY_OP_PUSH_NULL);
        } break;

        case EXP_ID:
        case EXP_DOT: {
            CompileGetIdOrDot(state, exp);
        } break;

        case EXP_BOOL: {
            GenerateCode(state, exp->boolean ? TINY_OP_PUSH_TRUE : TINY_OP_PUSH_FALSE);
        } break;

        case EXP_INT:
        case EXP_CHAR: {
            GeneratePushInt(state, exp->iValue);
        } break;

        case EXP_FLOAT: {
            GeneratePushFloat(state, exp->fValue);
        } break;

        case EXP_STRING: {
            GeneratePushString(state, exp->sIndex);
        } break;

        case EXP_CALL: {
            CompileCall(state, exp);
            GenerateCode(state, TINY_OP_GET_RETVAL);
        } break;

        case EXP_CONSTRUCTOR: {
            assert(exp->constructor.structTag->sstruct.defined);
            assert(sb_count(exp->constructor.args) <= UCHAR_MAX);

            int meCount = sb_count(exp->constructor.args);
            int tagCount = sb_count(exp->constructor.structTag->sstruct.fields);

            // Should be checked in ResolveTypes
            assert(meCount == tagCount);

            for (int i = 0; i < sb_count(exp->constructor.args); ++i) {
                CompileExpr(state, exp->constructor.args[i]);
            }

            GenerateCode(state, TINY_OP_PUSH_STRUCT);
            GenerateCode(state, sb_count(exp->constructor.args));
        } break;

        case EXP_CAST: {
            assert(exp->tag);
            assert(exp->cast.value);

            // TODO(Apaar): Once the cast actually does something, change this to
            // generate casting opcodes (ex. OP_INT_TO_FLOAT, OP_FLOAT_TO_INT, etc)
            CompileExpr(state, exp->cast.value);
        } break;

        case EXP_BINARY: {
            switch (exp->binary.op) {
                case TINY_TOK_PLUS: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_ADD);
                } break;

                case TINY_TOK_MINUS: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_SUB);
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
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_LOG_AND);
                } break;

                case TINY_TOK_LOG_OR: {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, TINY_OP_LOG_OR);
                } break;

                default:
                    ReportErrorE(state, exp, "Found assignment when expecting expression.\n");
                    break;
            }
        } break;

        case EXP_PAREN: {
            CompileExpr(state, exp->paren);
        } break;

        case EXP_UNARY: {
            switch (exp->unary.op) {
                case TINY_TOK_MINUS: {
                    if (exp->unary.exp->type == EXP_INT) {
                        GenerateCode(state, TINY_OP_PUSH_INT);
                        GenerateInt(state, -exp->unary.exp->iValue);
                    } else {
                        CompileExpr(state, exp->unary.exp);

                        GenerateCode(state, TINY_OP_PUSH_INT);
                        GenerateInt(state, -1);
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

        default:
            ReportErrorE(state, exp, "Got statement when expecting expression.\n");
            break;
    }
}

static void PatchBreakContinue(Tiny_State *state, Expr *body, int breakPC, int continuePC) {
    // For convenience
    if (!body) {
        return;
    }

    // If this encounters a loop, it will stop recursing since the
    // break/continue is lexical (and we don't support nested break
    // yet).

    switch (body->type) {
        // TODO(Apaar): These are the only types of blocks that are not loops, right? right?
        case EXP_IF: {
            PatchBreakContinue(state, body->ifx.body, breakPC, continuePC);
            PatchBreakContinue(state, body->ifx.alt, breakPC, continuePC);
        } break;

        case EXP_BLOCK: {
            for (int i = 0; i < sb_count(body->block); ++i) {
                PatchBreakContinue(state, body->block[i], breakPC, continuePC);
            }
        } break;

        case EXP_BREAK: {
            if (breakPC < 0) {
                ReportErrorE(
                    state, body,
                    "A break statement does not make sense here. It must be inside a loop.");
            }

            GenerateIntAt(state, breakPC, body->breakContinue.patchLoc);
        } break;

        case EXP_CONTINUE: {
            if (continuePC < 0) {
                ReportErrorE(state, body,
                             "A continue statement does not make sense here. It must be "
                             "inside a loop.");
            }

            GenerateIntAt(state, continuePC, body->breakContinue.patchLoc);
        } break;

        default:
            break;
    }
}

static void CompileStatement(Tiny_State *state, Expr *exp) {
    if (state->l.fileName) {
        GenerateCode(state, TINY_OP_FILE);
        GenerateInt(state, RegisterString(state, state->l.fileName));
    }

    GenerateCode(state, TINY_OP_LINE);
    GenerateInt(state, exp->lineNumber);

    switch (exp->type) {
        case EXP_CALL: {
            CompileCall(state, exp);
        } break;

        case EXP_BLOCK: {
            for (int i = 0; i < sb_count(exp->block); ++i) {
                CompileStatement(state, exp->block[i]);
            }
        } break;

        case EXP_BINARY: {
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
                    if (exp->binary.lhs->type == EXP_ID || exp->binary.lhs->type == EXP_DOT) {
                        switch (exp->binary.op) {
                            case TINY_TOK_PLUSEQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);

                                if (exp->binary.rhs->type == EXP_INT &&
                                    exp->binary.rhs->iValue == 1) {
                                    GenerateCode(state, TINY_OP_ADD1);
                                } else {
                                    CompileExpr(state, exp->binary.rhs);
                                    GenerateCode(state, TINY_OP_ADD);
                                }
                            } break;

                            case TINY_TOK_MINUSEQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);

                                if (exp->binary.rhs->type == EXP_INT &&
                                    exp->binary.rhs->iValue == 1) {
                                    GenerateCode(state, TINY_OP_SUB1);
                                } else {
                                    CompileExpr(state, exp->binary.rhs);
                                    GenerateCode(state, TINY_OP_SUB);
                                }
                            } break;

                            case TINY_TOK_STAREQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_MUL);
                            } break;

                            case TINY_TOK_SLASHEQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_DIV);
                            } break;

                            case TINY_TOK_PERCENTEQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_MOD);
                            } break;

                            case TINY_TOK_ANDEQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_AND);
                            } break;

                            case TINY_TOK_OREQUAL: {
                                CompileGetIdOrDot(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, TINY_OP_OR);
                            } break;

                            default:
                                CompileExpr(state, exp->binary.rhs);
                                break;
                        }

                        if (exp->binary.lhs->type == EXP_ID) {
                            if (!exp->binary.lhs->id.sym) {
                                // The variable being referenced doesn't exist
                                ReportErrorE(state, exp,
                                             "Assigning to undeclared identifier '%s'.\n",
                                             exp->binary.lhs->id.name);
                            }

                            if (exp->binary.lhs->id.sym->type == SYM_GLOBAL) {
                                GenerateCode(state, TINY_OP_SET);
                                GenerateInt(state, exp->binary.lhs->id.sym->var.index);
                            } else if (exp->binary.lhs->id.sym->type == SYM_LOCAL) {
                                GenerateCode(state, TINY_OP_SETLOCAL);
                                assert(exp->binary.lhs->id.sym->var.index <= 0xff);

                                GenerateCode(state, (Word)exp->binary.lhs->id.sym->var.index);
                            } else  // Probably a constant, can't change it
                            {
                                ReportErrorE(state, exp, "Cannot assign to id '%s'.\n",
                                             exp->binary.lhs->id.name);
                            }

                            exp->binary.lhs->id.sym->var.initialized = true;
                        } else {
                            assert(exp->binary.lhs->type == EXP_DOT);
                            assert(exp->binary.lhs->dot.lhs->tag->type == SYM_TAG_STRUCT);

                            int idx;

                            GetFieldTag(exp->binary.lhs->dot.lhs->tag, exp->binary.lhs->dot.field,
                                        &idx);

                            assert(idx >= 0 && idx <= UCHAR_MAX);

                            CompileExpr(state, exp->binary.lhs->dot.lhs);

                            GenerateCode(state, TINY_OP_STRUCT_SET);
                            GenerateCode(state, (Word)idx);
                        }
                    } else {
                        ReportErrorE(
                            state, exp,
                            "LHS of assignment operation must be a variable or dot expr\n");
                    }
                } break;

                default:
                    ReportErrorE(state, exp, "Invalid operation when expecting statement.\n");
                    break;
            }
        } break;

        case EXP_PROC: {
            GenerateCode(state, TINY_OP_GOTO);
            int skipGotoPc = GenerateInt(state, 0);

            state->functionPcs[exp->proc.decl->func.index] = sb_count(state->program);

            if (sb_count(exp->proc.decl->func.locals) > 0xff) {
                ReportErrorE(state, exp, "Exceeded maximum number of local variables (%d) allowed.",
                             0xff);
            }

            GenerateCode(state, TINY_OP_PUSH_NULL_N);
            GenerateCode(state, (Word)sb_count(exp->proc.decl->func.locals));

            if (exp->proc.body) {
                CompileStatement(state, exp->proc.body);
            }

            GenerateCode(state, TINY_OP_RETURN);
            GenerateIntAt(state, sb_count(state->program), skipGotoPc);
        } break;

        case EXP_IF: {
            CompileExpr(state, exp->ifx.cond);
            GenerateCode(state, TINY_OP_GOTOZ);

            int skipGotoPc = GenerateInt(state, 0);

            if (exp->ifx.body) CompileStatement(state, exp->ifx.body);

            GenerateCode(state, TINY_OP_GOTO);
            int exitGotoPc = GenerateInt(state, 0);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);

            if (exp->ifx.alt) CompileStatement(state, exp->ifx.alt);

            GenerateIntAt(state, sb_count(state->program), exitGotoPc);
        } break;

        case EXP_WHILE: {
            int condPc = sb_count(state->program);

            CompileExpr(state, exp->whilex.cond);

            GenerateCode(state, TINY_OP_GOTOZ);
            int skipGotoPc = GenerateInt(state, 0);

            if (exp->whilex.body) CompileStatement(state, exp->whilex.body);

            GenerateCode(state, TINY_OP_GOTO);
            GenerateInt(state, condPc);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);

            PatchBreakContinue(state, exp->whilex.body, sb_count(state->program), condPc);
        } break;

        case EXP_FOR: {
            CompileStatement(state, exp->forx.init);

            int condPc = sb_count(state->program);
            CompileExpr(state, exp->forx.cond);

            GenerateCode(state, TINY_OP_GOTOZ);
            int skipGotoPc = GenerateInt(state, 0);

            if (exp->forx.body) CompileStatement(state, exp->forx.body);

            CompileStatement(state, exp->forx.step);

            GenerateCode(state, TINY_OP_GOTO);
            GenerateInt(state, condPc);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);

            PatchBreakContinue(state, exp->forx.body, sb_count(state->program), condPc);
        } break;

        case EXP_RETURN: {
            if (exp->retExpr) {
                CompileExpr(state, exp->retExpr);
                GenerateCode(state, TINY_OP_RETURN_VALUE);
            } else
                GenerateCode(state, TINY_OP_RETURN);
        } break;

        case EXP_BREAK:
        case EXP_CONTINUE: {
            GenerateCode(state, TINY_OP_GOTO);
            exp->breakContinue.patchLoc = GenerateInt(state, 0);
        } break;

        default:
            ReportErrorE(state, exp,
                         "So this parsed successfully but when compiling I saw an expression where "
                         "I was expecting a statement.\n");
            break;
    }
}

static void CompileProgram(Tiny_State *state, Expr **program) {
    Expr **arr = program;
    for (int i = 0; i < sb_count(arr); ++i) {
        CompileStatement(state, arr[i]);
    }
}

static void DeleteProgram(Expr **program, Tiny_Context *ctx);

static void Expr_destroy(Expr *exp, Tiny_Context *ctx) {
    switch (exp->type) {
        case EXP_ID: {
            TFree(ctx, exp->id.name);
        } break;

        case EXP_NULL:
        case EXP_BOOL:
        case EXP_CHAR:
        case EXP_INT:
        case EXP_FLOAT:
        case EXP_STRING:
        case EXP_BREAK:
        case EXP_CONTINUE:
            break;

        case EXP_CALL: {
            TFree(ctx, exp->call.calleeName);
            for (int i = 0; i < sb_count(exp->call.args); ++i) Expr_destroy(exp->call.args[i], ctx);

            sb_free(ctx, exp->call.args);
        } break;

        case EXP_BLOCK: {
            for (int i = 0; i < sb_count(exp->block); ++i) {
                Expr_destroy(exp->block[i], ctx);
            }

            sb_free(ctx, exp->block);
        } break;

        case EXP_BINARY:
            Expr_destroy(exp->binary.lhs, ctx);
            Expr_destroy(exp->binary.rhs, ctx);
            break;
        case EXP_PAREN:
            Expr_destroy(exp->paren, ctx);
            break;

        case EXP_PROC: {
            if (exp->proc.body) Expr_destroy(exp->proc.body, ctx);
        } break;

        case EXP_IF:
            Expr_destroy(exp->ifx.cond, ctx);
            if (exp->ifx.body) Expr_destroy(exp->ifx.body, ctx);
            if (exp->ifx.alt) Expr_destroy(exp->ifx.alt, ctx);
            break;
        case EXP_WHILE:
            Expr_destroy(exp->whilex.cond, ctx);
            if (exp->whilex.body) Expr_destroy(exp->whilex.body, ctx);
            break;
        case EXP_RETURN:
            if (exp->retExpr) Expr_destroy(exp->retExpr, ctx);
            break;
        case EXP_UNARY:
            Expr_destroy(exp->unary.exp, ctx);
            break;

        case EXP_FOR: {
            Expr_destroy(exp->forx.init, ctx);
            Expr_destroy(exp->forx.cond, ctx);
            Expr_destroy(exp->forx.step, ctx);

            Expr_destroy(exp->forx.body, ctx);
        } break;

        case EXP_DOT: {
            Expr_destroy(exp->dot.lhs, ctx);
            TFree(ctx, exp->dot.field);
        } break;

        case EXP_CONSTRUCTOR: {
            for (int i = 0; i < sb_count(exp->constructor.args); ++i) {
                Expr_destroy(exp->constructor.args[i], ctx);
            }

            sb_free(ctx, exp->constructor.args);
        } break;

        case EXP_CAST: {
            Expr_destroy(exp->cast.value, ctx);
        } break;

        default:
            assert(false);
            break;
    }

    TFree(ctx, exp);
}

void DeleteProgram(Expr **program, Tiny_Context *ctx) {
    Expr **arr = program;
    for (int i = 0; i < sb_count(program); ++i) {
        Expr_destroy(arr[i], ctx);
    }

    sb_free(ctx, program);
}

static void CheckInitialized(Tiny_State *state) {
    const char *fmt = "Attempted to use uninitialized variable '%s'.\n";

    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *node = state->globalSymbols[i];

        assert(node->type != SYM_LOCAL);

        if (node->type == SYM_GLOBAL) {
            if (!node->var.initialized) {
                ReportErrorS(state, node, fmt, node->name);
            }
        } else if (node->type == SYM_FUNCTION) {
            // Only check locals, arguments are initialized implicitly
            for (int i = 0; i < sb_count(node->func.locals); ++i) {
                Symbol *local = node->func.locals[i];

                assert(local->type == SYM_LOCAL);

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
        Symbol *node = state->globalSymbols[i];

        if (node->type == SYM_FOREIGN_FUNCTION)
            state->foreignFunctions[node->foreignFunc.index] = node->foreignFunc.callee;
    }
}

static void CompileState(Tiny_State *state, Expr **prog) {
    // If this state was already compiled and it ends with an TINY_OP_HALT, We'll
    // just overwrite it
    if (sb_count(state->program) > 0) {
        if (state->program[sb_count(state->program) - 1] == TINY_OP_HALT) {
            stb__sbn(state->program) -= 1;
        }
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

    for (int i = 0; i < sb_count(prog); ++i) {
        ResolveTypes(state, prog[i]);
    }

    CompileProgram(state, prog);
    GenerateCode(state, TINY_OP_HALT);

    CheckInitialized(state);  // Done after compilation because it might have registered
                              // undefined functions during the compilation stage
}

void Tiny_CompileString(Tiny_State *state, const char *name, const char *string) {
    Tiny_InitLexer(&state->l, name, string, state->ctx);

    CurTok = 0;
    Expr **prog = ParseProgram(state);

    // Make sure all structs are defined
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol *s = state->globalSymbols[i];
        if (s->type == SYM_TAG_STRUCT && !s->sstruct.defined) {
            ReportErrorS(state, s, "Referenced undefined struct %s.", s->name);
        }
    }

    CompileState(state, prog);

    Tiny_DestroyLexer(&state->l);

    DeleteProgram(prog, &state->ctx);
}

void Tiny_CompileFile(Tiny_State *state, const char *filename) {
    FILE *file = fopen(filename, "rb");

    if (!file) {
        fprintf(stderr, "Error: Unable to open file '%s' for reading\n", filename);
        exit(1);
    }

    fseek(file, 0, SEEK_END);

    long len = ftell(file);

    char *s = TMalloc(&state->ctx, len + 1);

    rewind(file);

    fread(s, 1, len, file);
    s[len] = 0;

    fclose(file);

    Tiny_CompileString(state, filename, s);

    TFree(&state->ctx, s);
}

// tiny.c -- an bytecode-based interpreter for the tiny language
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#include "tiny.h"
#include "tiny_detail.h"
#include "stretchy_buffer.h"
#include "tiny_lexer.h"
#include "t_mem.h"
#include "tiny_util.h"

const Tiny_Value Tiny_Null = { TINY_VAL_NULL };

static int NumNumbers = 0;
static float Numbers[MAX_NUMBERS];

static int NumStrings = 0;
static char* Strings[MAX_STRINGS] = { 0 };

#define emalloc(size) malloc(size)
#define erealloc(mem, size) realloc(mem, size)

char* estrdup(const char* string)
{
    char* dupString = emalloc(strlen(string) + 1);
    strcpy(dupString, string);
    return dupString;
}

static void DeleteObject(Tiny_Object* obj)
{
    if(obj->type == TINY_VAL_STRING) free(obj->string);
    if (obj->type == TINY_VAL_NATIVE)
    {
        if (obj->nat.prop && obj->nat.prop->finalize)
            obj->nat.prop->finalize(obj->nat.addr);
    }

    free(obj);
}

static inline bool IsObject(Tiny_Value val)
{
    return val.type == TINY_VAL_STRING || val.type == TINY_VAL_NATIVE;
}

void Tiny_ProtectFromGC(Tiny_Value value)
{
    if(!IsObject(value))
        return;

    Tiny_Object* obj = value.obj;

    assert(obj);    
    
    if(obj->marked) return;
    
    if(obj->type == TINY_VAL_NATIVE)
    {
        if(obj->nat.prop && obj->nat.prop->protectFromGC)
            obj->nat.prop->protectFromGC(obj->nat.addr);
    }

    obj->marked = 1;
}

static void MarkAll(Tiny_StateThread* thread);

static void Sweep(Tiny_StateThread* thread)
{
    Tiny_Object** object = &thread->gcHead;
    while(*object)
    {
        if(!(*object)->marked)
        {
            Tiny_Object* unreached = *object;
            --thread->numObjects;
            *object = unreached->next;
            DeleteObject(unreached);
        }
        else
        {
            (*object)->marked = 0;
            object = &(*object)->next;
        }
    }
}

static void GarbageCollect(Tiny_StateThread* thread)
{
    MarkAll(thread);
    Sweep(thread);
    thread->maxNumObjects = thread->numObjects * 2;
}

const char* Tiny_ToString(const Tiny_Value value)
{
    if (value.type == TINY_VAL_CONST_STRING) return value.cstr;
    if(value.type != TINY_VAL_STRING) return NULL;

    return value.obj->string;
}

void* Tiny_ToAddr(const Tiny_Value value)
{
    if(value.type == TINY_VAL_LIGHT_NATIVE) return value.addr;
    if(value.type != TINY_VAL_NATIVE) return NULL;

    return value.obj->nat.addr;
}

const Tiny_NativeProp* Tiny_GetProp(const Tiny_Value value)
{
    if(value.type != TINY_VAL_NATIVE) return NULL;
    return value.obj->nat.prop;
}

static Tiny_Object* NewObject(Tiny_StateThread* thread, Tiny_ValueType type)
{
    Tiny_Object* obj = emalloc(sizeof(Tiny_Object));
    
    obj->type = type;
    obj->next = thread->gcHead;
    thread->gcHead = obj;
    obj->marked = 0;
    
    thread->numObjects++;
    
    return obj;
}

Tiny_Value Tiny_NewLightNative(void* ptr)
{
    Tiny_Value val;

    val.type = TINY_VAL_LIGHT_NATIVE;
    val.addr = ptr;

    return val;
}

Tiny_Value Tiny_NewNative(Tiny_StateThread* thread, void* ptr, const Tiny_NativeProp* prop)
{
    assert(thread && thread->state);
    
    // Make sure thread is alive
    assert(thread->pc >= 0);

    Tiny_Object* obj = NewObject(thread, TINY_VAL_NATIVE);
    
    obj->nat.addr = ptr;
    obj->nat.prop = prop;

    Tiny_Value val;

    val.type = TINY_VAL_NATIVE;
    val.obj = obj;

    return val;
}

Tiny_Value Tiny_NewBool(bool value)
{
    Tiny_Value val;

    val.type = TINY_VAL_BOOL;
    val.boolean = value;

    return val;
}

Tiny_Value Tiny_NewInt(int i)
{
    Tiny_Value val;

    val.type = TINY_VAL_INT;
    val.i = i;

    return val;
}

Tiny_Value Tiny_NewFloat(float f)
{
    Tiny_Value val;

    val.type = TINY_VAL_FLOAT;
    val.f = f;

    return val;
}

Tiny_Value Tiny_NewConstString(const char* str)
{
    assert(str);

    Tiny_Value val;

    val.type = TINY_VAL_CONST_STRING;
    val.cstr = str;
    
    return val;
}

Tiny_Value Tiny_NewString(Tiny_StateThread* thread, char* string)
{
    assert(thread && thread->state && string);
    
    Tiny_Object* obj = NewObject(thread, TINY_VAL_STRING);
    obj->string = string;

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    return val;
}

static void Symbol_destroy(Symbol* sym);

static Tiny_Value Lib_ToInt(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewInt(Tiny_ToFloat(args[0]));
}

static Tiny_Value Lib_ToFloat(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	return Tiny_NewFloat(Tiny_ToInt(args[0]));
}

Tiny_State* Tiny_CreateState(void)
{
    Tiny_State* state = emalloc(sizeof(Tiny_State));

    state->program = NULL;
    state->numGlobalVars = 0;
    
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

void Tiny_DeleteState(Tiny_State* state)
{
    sb_free(state->program);
    
    // Delete all symbols
    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol_destroy(state->globalSymbols[i]);
    }

    sb_free(state->globalSymbols);

    // Reset function and variable data
    free(state->functionPcs);
    free(state->foreignFunctions);

    free(state);
}

void Tiny_InitThread(Tiny_StateThread* thread, const Tiny_State* state)
{
    thread->state = state;

    thread->gcHead = NULL;
    thread->numObjects = 0;
    // TODO: Use INIT_GC_THRESH definition
    thread->maxNumObjects = 8;

    thread->globalVars = NULL;
    
    thread->pc = -1;
    thread->fp = thread->sp = 0;
    
    thread->retVal = Tiny_Null;

    thread->indirStackSize = 0;

    thread->fileName = NULL;
    thread->filePos = -1;

    thread->userdata = NULL;
}

static void AllocGlobals(Tiny_StateThread* thread)
{
    // If the global variables haven't been allocated yet,
    // do that
    if(!thread->globalVars)
    {
        thread->globalVars = emalloc(sizeof(Tiny_Value) * thread->state->numGlobalVars);
        memset(thread->globalVars, 0, sizeof(Tiny_Value) * thread->state->numGlobalVars);
    }
}

void Tiny_StartThread(Tiny_StateThread* thread)
{
    AllocGlobals(thread);

    // TODO: Eventually move to an actual entry point
    thread->pc = 0;
}

static bool ExecuteCycle(Tiny_StateThread* thread);

int Tiny_GetGlobalIndex(const Tiny_State* state, const char* name)
{
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* sym = state->globalSymbols[i];

        if (sym->type == SYM_GLOBAL && strcmp(sym->name, name) == 0) {
            return sym->var.index;
        }
    }

    return -1;
}

int Tiny_GetFunctionIndex(const Tiny_State* state, const char* name)
{
    for (int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* sym = state->globalSymbols[i];

        if (sym->type == SYM_FUNCTION && strcmp(sym->name, name) == 0) {
            return sym->func.index;
        }
    }

    return -1;
}

static void DoPushIndir(Tiny_StateThread* thread, int nargs);
static void DoPush(Tiny_StateThread* thread, Tiny_Value value);

Tiny_Value Tiny_GetGlobal(const Tiny_StateThread* thread, int globalIndex)
{
    assert(globalIndex >= 0 && globalIndex < thread->state->numGlobalVars);
    assert(thread->globalVars);
    
    return thread->globalVars[globalIndex];
}

void Tiny_SetGlobal(Tiny_StateThread* thread, int globalIndex, Tiny_Value value)
{
    assert(globalIndex >= 0 && globalIndex < thread->state->numGlobalVars);
    assert(thread->globalVars);

    thread->globalVars[globalIndex] = value;
}

Tiny_Value Tiny_CallFunction(Tiny_StateThread* thread, int functionIndex, const Tiny_Value* args, int count)
{
    assert(thread->state && functionIndex >= 0);

    int pc, fp, sp, indirStackSize;

    const char* fileName = thread->fileName;
	int filePos = thread->filePos;

    pc = thread->pc;
    fp = thread->fp;
    sp = thread->sp;
    indirStackSize = thread->indirStackSize;

    AllocGlobals(thread);

    for (int i = 0; i < count; ++i) {
        DoPush(thread, args[i]);
    }

    thread->pc = thread->state->functionPcs[functionIndex];
    DoPushIndir(thread, count);

    // Keep executing until the indir stack is restored (i.e. function is done)
    while (thread->indirStackSize > indirStackSize) {
        ExecuteCycle(thread);
    }

    Tiny_Value retVal = thread->retVal;

    thread->pc = pc;
    thread->fp = fp;
    thread->sp = sp;
    thread->indirStackSize = indirStackSize;

    thread->fileName = fileName;
    thread->filePos = filePos;

    return retVal;
}

bool Tiny_ExecuteCycle(Tiny_StateThread* thread)
{
    return ExecuteCycle(thread);
}

void Tiny_DestroyThread(Tiny_StateThread* thread)
{
    thread->pc = -1;

    // Free all objects in the gc list
    while(thread->gcHead)
    {
        Tiny_Object* next = thread->gcHead->next;
        DeleteObject(thread->gcHead);
        thread->gcHead = next;
    }

    // Free all global variables
    free(thread->globalVars);
}

static void MarkAll(Tiny_StateThread* thread)
{
    assert(thread->state);

    Tiny_ProtectFromGC(thread->retVal);

    for (int i = 0; i < thread->sp; ++i)
        Tiny_ProtectFromGC(thread->stack[i]);

    for (int i = 0; i < thread->state->numGlobalVars; ++i)
        Tiny_ProtectFromGC(thread->globalVars[i]);
}

static void GenerateCode(Tiny_State* state, Word inst)
{
    sb_push(state->program, inst);
}

static void GenerateInt(Tiny_State* state, int value)
{
    Word* wp = (Word*)(&value);
    for(int i = 0; i < 4; ++i)
        GenerateCode(state, *wp++);
}

static void GenerateIntAt(Tiny_State* state, int value, int pc)
{
    Word* wp = (Word*)(&value);
    for(int i = 0; i < 4; ++i)
        state->program[pc + i] = *wp++;
}

static int RegisterNumber(float value)
{
    for(int i = 0; i < NumNumbers; ++i)
    {
        if(Numbers[i] == value)
            return i;
    }

    assert(NumNumbers < MAX_NUMBERS);
    Numbers[NumNumbers++] = value;

    return NumNumbers - 1;
}

static int RegisterString(const char* string)
{
    for(int i = 0; i < NumStrings; ++i)
    {
        if(strcmp(Strings[i], string) == 0)
            return i;
    }

    assert(NumStrings < MAX_STRINGS);
	Strings[NumStrings++] = Tiny_Strdup(string);

    return NumStrings - 1; 
}

static Symbol* GetPrimTag(SymbolType type)
{
    static Symbol prims[] = {
        { SYM_TAG_VOID, (char*)"void", },
        { SYM_TAG_BOOL, (char*)"bool", },
        { SYM_TAG_INT, (char*)"int" },
        { SYM_TAG_FLOAT, (char*)"float" },
        { SYM_TAG_STR, (char*)"str" },
        { SYM_TAG_ANY, (char*)"any" }
    };

    return &prims[type - SYM_TAG_VOID];
}

static Symbol* Symbol_create(SymbolType type, const char* name, const Tiny_State* state)
{
    Symbol* sym = emalloc(sizeof(Symbol));

    sym->name = estrdup(name);
    sym->type = type;
	sym->pos = state->l.pos;

    return sym;
}

static void Symbol_destroy(Symbol* sym)
{
    if (sym->type == SYM_FUNCTION)
    {
        for(int i = 0; i < sb_count(sym->func.args); ++i) {
            Symbol* arg = sym->func.args[i];

            assert(arg->type == SYM_LOCAL);

            Symbol_destroy(arg);
        }
        
        sb_free(sym->func.args);
    
        for(int i = 0; i < sb_count(sym->func.locals); ++i) {
            Symbol* local = sym->func.locals[i];

            assert(local->type == SYM_LOCAL);

            Symbol_destroy(local);
        }

        sb_free(sym->func.locals);
    }
	else if (sym->type == SYM_FOREIGN_FUNCTION)
	{
		sb_free(sym->foreignFunc.argTags);
	}

    free(sym->name);
    free(sym);
}

static void OpenScope(Tiny_State* state)
{
    ++state->currScope;
}

static void CloseScope(Tiny_State* state)
{
    if (state->currFunc)
    {
        for(int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
            Symbol* sym = state->currFunc->func.locals[i];

            assert(sym->type == SYM_LOCAL);

            if(sym->var.scope == state->currScope) {
                sym->var.scopeEnded = true;
            }
        }
    }

    --state->currScope;
}

static Symbol* ReferenceVariable(Tiny_State* state, const char* name)
{
    if (state->currFunc)
    {
        // Check local variables
        for(int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
            Symbol* sym = state->currFunc->func.locals[i];

            assert(sym->type == SYM_LOCAL);

            // Make sure that it's available in the current scope too
            if (!sym->var.scopeEnded && strcmp(sym->name, name) == 0) {
                return sym;
            }
        }

        // Check arguments
        for(int i = 0; i < sb_count(state->currFunc->func.args); ++i) {
            Symbol* sym = state->currFunc->func.args[i];

            assert(sym->type == SYM_LOCAL);

            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
    }

    // Check global variables/constants
    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* sym = state->globalSymbols[i];

        if (sym->type == SYM_GLOBAL || sym->type == SYM_CONST)
        {
            if (strcmp(sym->name, name) == 0)
                return sym;
        }
    }

    // This variable doesn't exist
    return NULL;
}

static void ReportError(Tiny_State* state, const char* s, ...);

static Symbol* DeclareGlobalVar(Tiny_State* state, const char* name)
{
    Symbol* sym = ReferenceVariable(state, name);

    if(sym && (sym->type == SYM_GLOBAL || sym->type == SYM_CONST)) {
        ReportError(state, "Attempted to declare multiple global entities with the same name '%s'.", name);
    }


    Symbol* newNode = Symbol_create(SYM_GLOBAL, name, state);

    newNode->var.initialized = false;
    newNode->var.index = state->numGlobalVars;
    newNode->var.scope = 0;                    // Global variable scope don't matter
    newNode->var.scopeEnded = false;

    sb_push(state->globalSymbols, newNode);

    state->numGlobalVars += 1;

    return newNode;
}

// This expects nargs to be known beforehand because arguments are evaluated/pushed left-to-right
// so the first argument is actually at -nargs position relative to frame pointer
// We could reverse it, but this works out nicely for Foreign calls since we can just supply
// a pointer to the initial arg instead of reversing them.
static Symbol* DeclareArgument(Tiny_State* state, const char* name, Symbol* tag, int nargs)
{
    assert(state->currFunc);
    assert(tag);

    for(int i = 0; i < sb_count(state->currFunc->func.args); ++i) {
        Symbol* sym = state->currFunc->func.args[i];

        assert(sym->type == SYM_LOCAL);

        if (strcmp(sym->name, name) == 0) {
            ReportError(state, "Function '%s' takes multiple arguments with name '%s'.\n", state->currFunc->name, name);
        }
    }

    Symbol* newNode = Symbol_create(SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = -nargs + sb_count(state->currFunc->func.args);
    newNode->var.scope = 0;                                // These should be accessible anywhere in the function
    newNode->var.tag = tag;

    sb_push(state->currFunc->func.args, newNode);
    
    return newNode;
}

static Symbol* DeclareLocal(Tiny_State* state, const char* name)
{
    assert(state->currFunc);

    for(int i = 0; i < sb_count(state->currFunc->func.locals); ++i) {
        Symbol* sym = state->currFunc->func.locals[i];

        assert(sym->type == SYM_LOCAL);

        if (!sym->var.scopeEnded && strcmp(sym->name, name) == 0) {
            ReportError(state, "Function '%s' has multiple locals in the same scope with name '%s'.\n", state->currFunc->name, name);
        }
    }

    Symbol* newNode = Symbol_create(SYM_LOCAL, name, state);

    newNode->var.initialized = false;
    newNode->var.scopeEnded = false;
    newNode->var.index = sb_count(state->currFunc->func.locals);
    newNode->var.scope = state->currScope;

    sb_push(state->currFunc->func.locals, newNode);

    return newNode;
}

static Symbol* DeclareConst(Tiny_State* state, const char* name, Symbol* tag)
{
    Symbol* sym = ReferenceVariable(state, name);

    if (sym && (sym->type == SYM_CONST || sym->type == SYM_LOCAL || sym->type == SYM_GLOBAL)) {
        ReportError(state, "Attempted to define constant with the same name '%s' as another value.\n", name);
    }

    if (state->currFunc)
        fprintf(stderr, "Warning: Constant '%s' declared inside function bodies will still have global scope.\n", name);
    
    Symbol* newNode = Symbol_create(SYM_CONST, name, state);

    newNode->constant.tag = tag;

    sb_push(state->globalSymbols, newNode);

    return newNode;
}

static Symbol* DeclareFunction(Tiny_State* state, const char* name)
{
    Symbol* newNode = Symbol_create(SYM_FUNCTION, name, state);

    newNode->func.index = state->numFunctions;
    newNode->func.args = NULL;
    newNode->func.locals = NULL;

    sb_push(state->globalSymbols, newNode);

    state->numFunctions += 1;

    return newNode;
}

static Symbol* ReferenceFunction(Tiny_State* state, const char* name)
{
    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* node = state->globalSymbols[i];

        if ((node->type == SYM_FUNCTION || node->type == SYM_FOREIGN_FUNCTION) &&
            strcmp(node->name, name) == 0)
            return node;
    }

    return NULL;
}

static void BindFunction(Tiny_State* state, const char* name, Symbol** argTags, bool varargs, Symbol* returnTag, Tiny_ForeignFunction func)
{
    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* node = state->globalSymbols[i];

        if (node->type == SYM_FOREIGN_FUNCTION && strcmp(node->name, name) == 0)
        {
            fprintf(stderr, "There is already a foreign function bound to name '%s'.", name);
            exit(1);
        }
    }

    Symbol* newNode = Symbol_create(SYM_FOREIGN_FUNCTION, name, state);

    newNode->foreignFunc.index = state->numForeignFunctions;

    newNode->foreignFunc.argTags = argTags;
    newNode->foreignFunc.varargs = varargs;

    newNode->foreignFunc.returnTag = returnTag;

    newNode->foreignFunc.callee = func;

    sb_push(state->globalSymbols, newNode);

    state->numForeignFunctions += 1;
}

static Symbol* GetTagFromName(Tiny_State* state, const char* name);

void Tiny_RegisterType(Tiny_State* state, const char* name)
{
    Symbol* s = GetTagFromName(state, name);

	if (s) return;

    s = Symbol_create(SYM_TAG_FOREIGN, name, state);

    sb_push(state->globalSymbols, s);
}

static void ScanUntilDelim(const char** ps, char** buf)
{
    const char* s = *ps;

    while(*s && *s != '(' && *s != ')' && *s != ',') {
        if(isspace(*s)) {
            s += 1;
            continue;
        }

		sb_push(*buf, *s++);
    }

	sb_push(*buf, 0);

    *ps = s;
}

void Tiny_BindFunction(Tiny_State* state, const char* sig, Tiny_ForeignFunction func)
{
	char* name = NULL;

    ScanUntilDelim(&sig, &name);

    if(!sig[0]) {
        // Just the name
        BindFunction(state, name, NULL, true, GetPrimTag(SYM_TAG_ANY), func);
        return;
    }

    sig += 1;

    Symbol** argTags = NULL;
    bool varargs = false;
	char* buf = NULL;

    while(*sig != ')' && !varargs) {
        ScanUntilDelim(&sig, &buf);

        if(strcmp(buf, "...") == 0) {
            varargs = true;

			sb_free(buf);
			buf = NULL;
            break;
        } else {
            Symbol* s = GetTagFromName(state, buf);

            assert(s);

            sb_push(argTags, s);

			sb_free(buf);
			buf = NULL;
        }
		
		if (*sig == ',') ++sig;
    }

    assert(*sig == ')');

    sig += 1;

    Symbol* returnTag = GetPrimTag(SYM_TAG_ANY);

    if(*sig == ':') {
        sig += 1;

        ScanUntilDelim(&sig, &buf);

        returnTag = GetTagFromName(state, buf);
        assert(returnTag);

		sb_free(buf);
    }

    BindFunction(state, name, argTags, varargs, returnTag, func);
}

void Tiny_BindConstBool(Tiny_State* state, const char* name, bool b)
{
	DeclareConst(state, name, GetPrimTag(SYM_TAG_BOOL))->constant.bValue = b;
}

void Tiny_BindConstInt(Tiny_State* state, const char* name, int i)
{
	DeclareConst(state, name, GetPrimTag(SYM_TAG_INT))->constant.iValue = i;
}

void Tiny_BindConstFloat(Tiny_State* state, const char* name, float f)
{
	DeclareConst(state, name, GetPrimTag(SYM_TAG_FLOAT))->constant.fIndex = RegisterNumber(f);
}

void Tiny_BindConstString(Tiny_State* state, const char* name, const char* string)
{
	DeclareConst(state, name, GetPrimTag(SYM_TAG_STR))->constant.sIndex = RegisterString(string);
}

enum
{
    OP_PUSH_NULL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,

	OP_PUSH_INT,
	OP_PUSH_FLOAT,
    OP_PUSH_STRING,

    OP_POP,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_OR,
    OP_AND,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,

    OP_EQU,

    OP_LOG_NOT,
    OP_LOG_AND,
    OP_LOG_OR,

    OP_PRINT,
    
    OP_SET,
    OP_GET,
    
    OP_READ,
    
    OP_GOTO,
    OP_GOTOZ,

    OP_CALL,
    OP_RETURN,
    OP_RETURN_VALUE,

    OP_CALLF,

    OP_GETLOCAL,
    OP_SETLOCAL,

    OP_GET_RETVAL,

    OP_HALT,

    OP_FILE,
    OP_POS,
};

static int ReadInteger(Tiny_StateThread* thread)
{
    assert(thread->state);

    int val = *(int*)(&thread->state->program[thread->pc]);
    thread->pc += sizeof(int) / sizeof(Word);

    return val;
}

static void DoPush(Tiny_StateThread* thread, Tiny_Value value)
{
    thread->stack[thread->sp++] = value;
}

inline Tiny_Value DoPop(Tiny_StateThread* thread)
{	    
    return thread->stack[--thread->sp];
}

static void DoRead(Tiny_StateThread* thread)
{
    char* buffer = emalloc(1);
    size_t bufferLength = 0;
    size_t bufferCapacity = 1;
    
    int c = getc(stdin);
    int i = 0;

    while(c != '\n')
    {
        if(bufferLength + 1 >= bufferCapacity)
        {
            bufferCapacity *= 2;
            buffer = erealloc(buffer, bufferCapacity);
        }
        
        buffer[i++] = c;
        c = getc(stdin);
    }
    
    buffer[i] = '\0';
    
    Tiny_Object* obj = NewObject(thread, TINY_VAL_STRING);
    obj->string = buffer;

    Tiny_Value val;

    val.type = TINY_VAL_STRING;
    val.obj = obj;

    DoPush(thread, val);
}

static void DoPushIndir(Tiny_StateThread* thread, int nargs)
{
    assert(thread->indirStackSize + 3 <= TINY_THREAD_INDIR_SIZE);

    thread->indirStack[thread->indirStackSize++] = nargs;
    thread->indirStack[thread->indirStackSize++] = thread->fp;
    thread->indirStack[thread->indirStackSize++] = thread->pc;

    thread->fp = thread->sp;
}

static void DoPopIndir(Tiny_StateThread* thread)
{
    assert(thread->indirStackSize >= 3);

    thread->sp = thread->fp;

    int prevPc = thread->indirStack[--thread->indirStackSize];
    int prevFp = thread->indirStack[--thread->indirStackSize];
    int nargs = thread->indirStack[--thread->indirStackSize];
    
    thread->sp -= nargs;
    thread->fp = prevFp;
    thread->pc = prevPc;
}

inline static bool ExpectBool(const Tiny_Value value)
{
    assert(value.type == TINY_VAL_BOOL);
    return value.boolean;
}

static bool ExecuteCycle(Tiny_StateThread* thread)
{
    assert(thread && thread->state);

    if (thread->pc < 0) return false;

    const Tiny_State* state = thread->state;

    switch(state->program[thread->pc])
    {
        case OP_PUSH_NULL:
        {
            ++thread->pc;
            DoPush(thread, Tiny_Null);
        } break;
        
        case OP_PUSH_TRUE:
        {
            ++thread->pc;
            DoPush(thread, Tiny_NewBool(true));
        } break;

        case OP_PUSH_FALSE:
        {
            ++thread->pc;
            DoPush(thread, Tiny_NewBool(false));
        } break;

		case OP_PUSH_INT:
		{
            ++thread->pc;
            
			thread->stack[thread->sp].type = TINY_VAL_INT;
			thread->stack[thread->sp].i = *(int*)(&thread->state->program[thread->pc]);
			thread->pc += 4;
			thread->sp += 1;
		} break;

		case OP_PUSH_FLOAT:
		{
            ++thread->pc;
            
            int fIndex = ReadInteger(thread);

            DoPush(thread, Tiny_NewFloat(Numbers[fIndex]));
		} break;

        case OP_PUSH_STRING:
        {
            ++thread->pc;

            int stringIndex = ReadInteger(thread);

            DoPush(thread, Tiny_NewConstString(Strings[stringIndex]));
        } break;
        
        case OP_POP:
        {
            DoPop(thread);
            ++thread->pc;
		} break;

#define BIN_OP(OP, operator) case OP_##OP: { \
			Tiny_Value val2 = DoPop(thread); \
			Tiny_Value val1 = DoPop(thread); \
			if(val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_INT) DoPush(thread, Tiny_NewFloat(val1.f operator (float)val2.i)); \
			else if(val1.type == TINY_VAL_INT && val2.type == TINY_VAL_FLOAT) DoPush(thread, Tiny_NewFloat((float)val1.i operator val2.f)); \
			else if(val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_FLOAT) DoPush(thread, Tiny_NewFloat(val1.f operator val2.f)); \
			else DoPush(thread, Tiny_NewInt(val1.i operator val2.i)); \
			++thread->pc; \
		} break;

#define BIN_OP_INT(OP, operator) case OP_##OP: { Tiny_Value val2 = DoPop(thread); Tiny_Value val1 = DoPop(thread); DoPush(thread, Tiny_NewInt((int)val1.i operator (int)val2.i)); ++thread->pc; } break;

#define REL_OP(OP, operator) case OP_##OP: { \
			Tiny_Value val2 = DoPop(thread); \
			Tiny_Value val1 = DoPop(thread); \
			if(val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_INT) DoPush(thread, Tiny_NewBool(val1.f operator (float)val2.i)); \
			else if(val1.type == TINY_VAL_INT && val2.type == TINY_VAL_FLOAT) DoPush(thread, Tiny_NewBool((float)val1.i operator val2.f)); \
			else if(val1.type == TINY_VAL_FLOAT && val2.type == TINY_VAL_FLOAT) DoPush(thread, Tiny_NewBool(val1.f operator val2.f)); \
			else DoPush(thread, Tiny_NewBool(val1.i operator val2.i)); \
			++thread->pc; \
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

        case OP_EQU:
        {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);

            bool bothStrings = ((a.type == TINY_VAL_CONST_STRING && b.type == TINY_VAL_STRING) ||
                (a.type == TINY_VAL_STRING && b.type == TINY_VAL_CONST_STRING));

            if (a.type != b.type && !bothStrings)
                DoPush(thread, Tiny_NewBool(false));
            else
            {
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
                else if (a.type == TINY_VAL_CONST_STRING) 
                {
                    if (b.type == TINY_VAL_CONST_STRING && a.cstr == b.cstr) DoPush(thread, Tiny_NewBool(true));
                    else DoPush(thread, Tiny_NewBool(strcmp(a.cstr, Tiny_ToString(b)) == 0));
                }
                else if (a.type == TINY_VAL_NATIVE)
                    DoPush(thread, Tiny_NewBool(a.obj->nat.addr == b.obj->nat.addr));
                else if (a.type == TINY_VAL_LIGHT_NATIVE)
                    DoPush(thread, Tiny_NewBool(a.addr == b.addr));
            }
        } break;

        case OP_LOG_NOT:
        {
            ++thread->pc;
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(!ExpectBool(a)));
        } break;

        case OP_LOG_AND:
        {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(ExpectBool(a) && ExpectBool(b)));
        } break;

        case OP_LOG_OR:
        {
            ++thread->pc;
            Tiny_Value b = DoPop(thread);
            Tiny_Value a = DoPop(thread);

            DoPush(thread, Tiny_NewBool(ExpectBool(a) || ExpectBool(b)));
        } break;

        case OP_PRINT:
        {
            Tiny_Value val = DoPop(thread);
            if(val.type == TINY_VAL_INT) printf("%d\n", val.i);
            else if(val.type == TINY_VAL_FLOAT) printf("%f\n", val.f);
            else if (val.obj->type == TINY_VAL_STRING) printf("%s\n", val.obj->string);
            else if (val.obj->type == TINY_VAL_CONST_STRING) printf("%s\n", val.cstr);
            else if (val.obj->type == TINY_VAL_NATIVE) printf("<native at %p>\n", val.obj->nat.addr);
            else if (val.obj->type == TINY_VAL_LIGHT_NATIVE) printf("<light native at %p>\n", val.obj->nat.addr);
            ++thread->pc;
        } break;

        case OP_SET:
        {
            ++thread->pc;
            int varIdx = ReadInteger(thread);
            thread->globalVars[varIdx] = DoPop(thread);
        } break;
        
        case OP_GET:
        {
            ++thread->pc;
            int varIdx = ReadInteger(thread);
            DoPush(thread, thread->globalVars[varIdx]); 
        } break;
        
        case OP_READ:
        {
            DoRead(thread);
            ++thread->pc;
        } break;
        
        case OP_GOTO:
        {
            ++thread->pc;
            int newPc = ReadInteger(thread);
            thread->pc = newPc;
        } break;
        
        case OP_GOTOZ:
        {
            ++thread->pc;
            int newPc = ReadInteger(thread);
            
            Tiny_Value val = DoPop(thread);

            if(!ExpectBool(val))
                thread->pc = newPc;
        } break;
        
        case OP_CALL:
        {
            ++thread->pc;
            int nargs = ReadInteger(thread);
            int pcIdx = ReadInteger(thread);
            
            DoPushIndir(thread, nargs);
            thread->pc = state->functionPcs[pcIdx];
        } break;
        
        case OP_RETURN:
        {
            thread->retVal = Tiny_Null;

            DoPopIndir(thread);
        } break;
        
        case OP_RETURN_VALUE:
        {
            thread->retVal = DoPop(thread);
            DoPopIndir(thread);
        } break;
        
        case OP_CALLF:
        {
            ++thread->pc;
            
            int nargs = ReadInteger(thread);
            int fIdx = ReadInteger(thread);

            // the state of the stack prior to the function arguments being pushed
            int prevSize = thread->sp - nargs;

            thread->retVal = state->foreignFunctions[fIdx](thread, &thread->stack[prevSize], nargs);
            
            // Resize the stack so that it has the arguments removed
            thread->sp = prevSize;
        } break;

        case OP_GETLOCAL:
        {
            ++thread->pc;
            int localIdx = ReadInteger(thread);
            DoPush(thread, thread->stack[thread->fp + localIdx]);
        } break;
        
        case OP_SETLOCAL:
        {
            ++thread->pc;
            int localIdx = ReadInteger(thread);
            Tiny_Value val = DoPop(thread);
            thread->stack[thread->fp + localIdx] = val;
        } break;

        case OP_GET_RETVAL:
        {
            ++thread->pc;
            DoPush(thread, thread->retVal);
        } break;

        case OP_HALT:
        {
            thread->fileName = NULL;
            thread->filePos = -1;

            thread->pc = -1;
        } break;

        case OP_FILE:
        {
            ++thread->pc;
            int stringIndex = ReadInteger(thread);

            thread->fileName = Strings[stringIndex];
        } break;

		case OP_POS:
        {
            ++thread->pc;
            int line = ReadInteger(thread);

            thread->filePos = line;
        } break;
    }

    // Only collect garbage in between iterations
    if (thread->numObjects >= thread->maxNumObjects)
        GarbageCollect(thread);

    return true;
}

typedef enum
{
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
} ExprType;

typedef struct sExpr
{
    ExprType type;
    Tiny_TokenPos pos;

    Symbol* tag;

    union
    {
        bool boolean;

        int iValue;
        int fIndex;
        int sIndex;

        struct
        {
            char* name;
            Symbol* sym;
        } id;

        struct
        {
            char* calleeName;
            struct sExpr** args; // array
        } call;
        
        struct
        {
            struct sExpr* lhs;
            struct sExpr* rhs;
            int op;
        } binary;
        
        struct sExpr* paren;
        
        struct 
        {
            int op;
            struct sExpr* exp;
        } unary;
        
        struct sExpr** block;    // array

        struct
        {
            Symbol* decl;
            struct sExpr* body;
        } proc;

        struct
        {
            struct sExpr* cond;
            struct sExpr* body;
            struct sExpr* alt;
        } ifx;
        
        struct
        {
            struct sExpr* cond;
            struct sExpr* body;
        } whilex;

        struct
        {
            struct sExpr* init;
            struct sExpr* cond;
            struct sExpr* step;
            struct sExpr* body;
        } forx;
        
        struct sExpr* retExpr;
    };
} Expr;

static Expr* Expr_create(ExprType type, const Tiny_State* state)
{
    Expr* exp = emalloc(sizeof(Expr));

    exp->pos = state->l.pos;
    exp->type = type;

    exp->tag = NULL;
    
    return exp;
}

int CurTok;

static int GetNextToken(Tiny_State* state)
{
    CurTok = Tiny_GetToken(&state->l);
    return CurTok;
}

static Expr* ParseExpr(Tiny_State* state);

static void ReportError(Tiny_State* state, const char* s, ...)
{
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, state->l.pos, s, args);

    va_end(args);
    exit(1);
}

static void ReportErrorE(Tiny_State* state, const Expr* exp, const char* s, ...)
{
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, exp->pos, s, args);

    va_end(args);
    exit(1);
}

static void ReportErrorS(Tiny_State* state, const Symbol* sym, const char* s, ...)
{
    va_list args;
    va_start(args, s);

    Tiny_ReportErrorV(state->l.fileName, state->l.src, sym->pos, s, args);

    va_end(args);
    exit(1);
}

static void ExpectToken(Tiny_State* state, int tok, const char* msg)
{
    if (CurTok != tok) ReportError(state, msg);
}

static Symbol* GetTagFromName(Tiny_State* state, const char* name)
{
    if(strcmp(name, "void") == 0) return GetPrimTag(SYM_TAG_VOID);
    else if(strcmp(name, "bool") == 0) return GetPrimTag(SYM_TAG_BOOL);
	else if(strcmp(name, "int") == 0) return GetPrimTag(SYM_TAG_INT);
	else if(strcmp(name, "float") == 0) return GetPrimTag(SYM_TAG_FLOAT);
	else if(strcmp(name, "str") == 0) return GetPrimTag(SYM_TAG_STR);
    else if(strcmp(name, "any") == 0) return GetPrimTag(SYM_TAG_ANY);
    else {
        for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
            Symbol* s = state->globalSymbols[i];

            if(s->type == SYM_TAG_FOREIGN && strcmp(s->name, name) == 0) {
                return s;
            }
        }
    }

    return NULL;
}

static Symbol* ParseType(Tiny_State* state)
{
    ExpectToken(state, TINY_TOK_IDENT, "Expected identifier for typename.");

	Symbol* s = GetTagFromName(state, state->l.lexeme);

	if (!s) {
		ReportError(state, "%s doesn't name a type.", state->l.lexeme);
	}

	GetNextToken(state);

	return s;
}

static Expr* ParseIf(Tiny_State* state)
{
    Expr* exp = Expr_create(EXP_IF, state);

    GetNextToken(state);

    exp->ifx.cond = ParseExpr(state);
    exp->ifx.body = ParseExpr(state);

    if (CurTok == TINY_TOK_ELSE)
    {
        GetNextToken(state);
        exp->ifx.alt = ParseExpr(state);
    }
    else
        exp->ifx.alt = NULL;

    return exp;
}

static Expr* ParseBlock(Tiny_State* state)
{
    assert(CurTok == TINY_TOK_OPENCURLY);

    Expr* exp = Expr_create(EXP_BLOCK, state);

    exp->block = NULL;

    GetNextToken(state);

    OpenScope(state);

    while (CurTok != TINY_TOK_CLOSECURLY)
    {
        Expr* e = ParseExpr(state);
        assert(e);
        
        sb_push(exp->block, e);
    }

    GetNextToken(state);

    CloseScope(state);

    return exp;
}

static Expr* ParseFunc(Tiny_State* state)
{
    assert(CurTok == TINY_TOK_FUNC);

    if(state->currFunc)
    {
        ReportError(state, "Attempted to define function inside of function '%s'.", state->currFunc->name);
    }

    Expr* exp = Expr_create(EXP_PROC, state);

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_IDENT, "Function name must be identifier!");

	exp->proc.decl = DeclareFunction(state, state->l.lexeme);
    state->currFunc = exp->proc.decl;

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_OPENPAREN, "Expected '(' after function name");
    GetNextToken(state);

	typedef struct
	{
		char* name;
		Symbol* tag;
	} Arg;

	Arg* args = NULL; // array

    while(CurTok != TINY_TOK_CLOSEPAREN)
    {
        ExpectToken(state, TINY_TOK_IDENT, "Expected identifier in function parameter list");

		Arg arg;

        arg.name = Tiny_Strdup(state->l.lexeme);
        GetNextToken(state);

        if(CurTok != TINY_TOK_COLON) {
            ReportError(state, "Expected ':' after %s", arg.name);
        }

        GetNextToken(state);

		arg.tag = ParseType(state);

		sb_push(args, arg);

        if (CurTok != TINY_TOK_CLOSEPAREN && CurTok != TINY_TOK_COMMA)
        {
            ReportError(state, "Expected ')' or ',' after parameter name in function parameter list.");
        }

        if(CurTok == TINY_TOK_COMMA) GetNextToken(state);
    }

    for (int i = 0; i < sb_count(args); ++i) {
        DeclareArgument(state, args[i].name, args[i].tag, sb_count(args));
		free(args[i].name);
    }

	sb_free(args);

    GetNextToken(state);

    ExpectToken(state, TINY_TOK_COLON, "Expected ':' after func prototype.");

    GetNextToken(state);

    exp->proc.decl->func.returnTag = ParseType(state);

    OpenScope(state);

    exp->proc.body = ParseExpr(state);

    CloseScope(state);

    state->currFunc = NULL;

    return exp;
}

static Expr* ParseFactor(Tiny_State* state)
{
    switch(CurTok)
    {
        case TINY_TOK_NULL:
        {
            Expr* exp = Expr_create(EXP_NULL, state);

            GetNextToken(state);

            return exp;
        } break;

		case TINY_TOK_BOOL:
        {
            Expr* exp = Expr_create(EXP_BOOL, state);

            exp->boolean = state->l.bValue;

            GetNextToken(state);

            return exp;
        } break;

        case TINY_TOK_OPENCURLY: return ParseBlock(state);

        case TINY_TOK_IDENT:
        {
            char* ident = Tiny_Strdup(state->l.lexeme);
            GetNextToken(state);

            if(CurTok != TINY_TOK_OPENPAREN)
            {
                Expr* exp;
                
                exp = Expr_create(EXP_ID, state);
                
                exp->id.sym = ReferenceVariable(state, ident);
                exp->id.name = ident;

                return exp;
            }
            
            Expr* exp = Expr_create(EXP_CALL, state);

            exp->call.args = NULL;
            
            GetNextToken(state);
            
            while(CurTok != TINY_TOK_CLOSEPAREN)
            {
                sb_push(exp->call.args, ParseExpr(state));

                if(CurTok == TINY_TOK_COMMA) GetNextToken(state);
                else if(CurTok != TINY_TOK_CLOSEPAREN)
                {
                    ReportError(state, "Expected ')' after call.");
                }
            }

            exp->call.calleeName = ident;

            GetNextToken(state);
            return exp;
        } break;
        
		case TINY_TOK_MINUS: case TINY_TOK_BANG:
        {
            int op = CurTok;
            GetNextToken(state);
            Expr* exp = Expr_create(EXP_UNARY, state);
            exp->unary.op = op;
            exp->unary.exp = ParseFactor(state);

            return exp;
        } break;
        
		case TINY_TOK_CHAR:
        {
            Expr* exp = Expr_create(EXP_CHAR, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
        } break;

		case TINY_TOK_INT:
		{
            Expr* exp = Expr_create(EXP_INT, state);
            exp->iValue = state->l.iValue;
            GetNextToken(state);
            return exp;
		} break;

		case TINY_TOK_FLOAT:
		{
            Expr* exp = Expr_create(EXP_FLOAT, state);
			exp->fIndex = RegisterNumber(state->l.fValue);
            GetNextToken(state);
            return exp;
		} break;

        case TINY_TOK_STRING:
        {
            Expr* exp = Expr_create(EXP_STRING, state);
            exp->sIndex = RegisterString(state->l.lexeme);
            GetNextToken(state);
            return exp;
        } break;
        
        case TINY_TOK_FUNC: return ParseFunc(state);
        
        case TINY_TOK_IF: return ParseIf(state);
        
        case TINY_TOK_WHILE:
        {
            GetNextToken(state);
            Expr* exp = Expr_create(EXP_WHILE, state);

            exp->whilex.cond = ParseExpr(state);

            OpenScope(state);
            
            exp->whilex.body = ParseExpr(state);
            
            CloseScope(state);

            return exp;
        } break;

        case TINY_TOK_FOR:
        {
            GetNextToken(state);
            Expr* exp = Expr_create(EXP_FOR, state);
            
            // Every local declared after this is scoped to the for
            OpenScope(state);

            exp->forx.init = ParseExpr(state);

            ExpectToken(state, TINY_TOK_SEMI, "Expected ';' after for initializer.");

            GetNextToken(state);

            exp->forx.cond = ParseExpr(state);

            ExpectToken(state, TINY_TOK_SEMI, "Expected ';' after for condition.");

            GetNextToken(state);

            exp->forx.step = ParseExpr(state);

            exp->forx.body = ParseExpr(state);

            CloseScope(state);

            return exp;
        } break;
        
        case TINY_TOK_RETURN:
        {
            if(!state->currFunc) {
                ReportError(state, "Attempted to return from outside a function. Why? Why would you do that? Why would you do any of that?");
            }

            GetNextToken(state);
            Expr* exp = Expr_create(EXP_RETURN, state);
            if(CurTok == TINY_TOK_SEMI)
            {
                GetNextToken(state);    
                exp->retExpr = NULL;
                return exp;
            }

            if(state->currFunc->func.returnTag->type == SYM_TAG_VOID) {
                ReportError(state, "Attempted to return value from function which is supposed to return nothing (void).");
            }

            exp->retExpr = ParseExpr(state);
            return exp;
        } break;

        case TINY_TOK_OPENPAREN:
        {
            GetNextToken(state);
            Expr* inner = ParseExpr(state);

			ExpectToken(state, TINY_TOK_CLOSEPAREN, "Expected matching ')' after previous '('");
            GetNextToken(state);
            
            Expr* exp = Expr_create(EXP_PAREN, state);
            exp->paren = inner;
            return exp;
        } break;
        
        default: break;
    }

    ReportError(state, "Unexpected token '%s'\n", state->l.lexeme);
    return NULL;
}

static int GetTokenPrec()
{
    int prec = -1;
    switch(CurTok)
    {
        case TINY_TOK_STAR: case TINY_TOK_SLASH: case TINY_TOK_PERCENT: case TINY_TOK_AND: case TINY_TOK_OR: prec = 5; break;
        
        case TINY_TOK_PLUS: case TINY_TOK_MINUS:                prec = 4; break;
        
        case TINY_TOK_LTE: case TINY_TOK_GTE:
        case TINY_TOK_EQUALS: case TINY_TOK_NOTEQUALS:
        case TINY_TOK_LT: case TINY_TOK_GT:                prec = 3; break;
        
        case TINY_TOK_LOG_AND: case TINY_TOK_LOG_OR:        prec = 2; break;

        case TINY_TOK_PLUSEQUAL: case TINY_TOK_MINUSEQUAL: case TINY_TOK_STAREQUAL: case TINY_TOK_SLASHEQUAL:
        case TINY_TOK_PERCENTEQUAL: case TINY_TOK_ANDEQUAL: case TINY_TOK_OREQUAL:
        case TINY_TOK_DECLARECONST:
        case TINY_TOK_DECLARE: case TINY_TOK_COLON: case TINY_TOK_EQUAL:                        prec = 1; break;
    }
    
    return prec;
}

static Expr* ParseBinRhs(Tiny_State* state, int exprPrec, Expr* lhs)
{
    while(1)
    {
        int prec = GetTokenPrec();
        
        if(prec < exprPrec)
            return lhs;

        int binOp = CurTok;

        // They're trying to declare a variable (we can only know this when we 
        // encounter this token)
        if (binOp == TINY_TOK_DECLARE) {
            if (lhs->type != EXP_ID)
            {
                ReportError(state, "Expected identifier to the left-hand side of ':='.\n");
            }

            // If we're inside a function declare a local, otherwise a global
            if (state->currFunc)
                lhs->id.sym = DeclareLocal(state, lhs->id.name);
            else
                lhs->id.sym = DeclareGlobalVar(state, lhs->id.name);
        } else if(binOp == TINY_TOK_COLON) {
            // They're trying to declare a variable with explicit type
            if (state->currFunc)
                lhs->id.sym = DeclareLocal(state, lhs->id.name);
            else
                lhs->id.sym = DeclareGlobalVar(state, lhs->id.name);

			GetNextToken(state);

			lhs->id.sym->var.tag = ParseType(state);

			binOp = TINY_TOK_EQUAL;
        }

        GetNextToken(state);

        Expr* rhs = ParseFactor(state);
        int nextPrec = GetTokenPrec();
        
        if(prec < nextPrec)
            rhs = ParseBinRhs(state, prec + 1, rhs);

        if (binOp == TINY_TOK_DECLARECONST)
        {
            if (lhs->type != EXP_ID)
            {
                ReportError(state, "Expected identifier to the left-hand side of '::'.\n");
            }

			if (rhs->type == EXP_BOOL) {
				DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_BOOL))->constant.bValue = rhs->boolean;
			} else if (rhs->type == EXP_CHAR) {
				DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_INT))->constant.iValue = rhs->iValue;
			} else if (rhs->type == EXP_INT) {
				DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_INT))->constant.iValue = rhs->iValue;
			} else if(rhs->type == EXP_FLOAT) {
				DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_FLOAT))->constant.fIndex = rhs->fIndex;
			} else if (rhs->type == EXP_STRING) {
				DeclareConst(state, lhs->id.name, GetPrimTag(SYM_TAG_STR))->constant.sIndex = rhs->sIndex;
			} else {
                ReportError(state, "Expected number or string to be bound to constant '%s'.\n", lhs->id.name);
            }
        }

        Expr* newLhs = Expr_create(EXP_BINARY, state);
        
        newLhs->binary.lhs = lhs;
        newLhs->binary.rhs = rhs;
        newLhs->binary.op = binOp;
        
        lhs = newLhs;
    }
}

static Expr* ParseExpr(Tiny_State* state)
{
    Expr* factor = ParseFactor(state);
    return ParseBinRhs(state, 0, factor);
}

static Expr** ParseProgram(Tiny_State* state)
{
    GetNextToken(state);
        
    if(CurTok != TINY_TOK_EOF)
    {    
        Expr** arr = NULL;

        while(CurTok != TINY_TOK_EOF)
        {
            Expr* stmt = ParseExpr(state);
            sb_push(arr, stmt);
        }

        return arr;
    }

    return NULL;
}

static const char* GetTagName(const Symbol* tag)
{
    assert(tag);
    return tag->name;
}

// Checks if types can be narrowed/widened to be equal
static bool CompareTags(const Symbol* a, const Symbol* b)
{
    if(a->type == SYM_TAG_VOID) {
        return b->type == SYM_TAG_VOID;
    }
    
    if(a->type == SYM_TAG_ANY || b->type == SYM_TAG_ANY) {
        return true;
    }

    if(a->type == b->type) {
        if(a->type == SYM_TAG_FOREIGN) {
            // Foreign tags are singletons
            return a == b;
        }

        return true;
    }

    return false;
}

static void ResolveTypes(Tiny_State* state, Expr* exp)
{
    if(exp->tag) return;

    switch(exp->type)
    {
        case EXP_NULL: exp->tag = GetPrimTag(SYM_TAG_ANY); break;
        case EXP_BOOL: exp->tag = GetPrimTag(SYM_TAG_BOOL); break;
        case EXP_CHAR: exp->tag = GetPrimTag(SYM_TAG_INT); break;
        case EXP_INT: exp->tag = GetPrimTag(SYM_TAG_INT); break;
        case EXP_FLOAT: exp->tag = GetPrimTag(SYM_TAG_FLOAT); break;
        case EXP_STRING: exp->tag = GetPrimTag(SYM_TAG_STR); break;
        
        case EXP_ID: {
            if(!exp->id.sym) {
                ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n", exp->id.name);
            }

            assert(exp->id.sym->type == SYM_GLOBAL ||
                   exp->id.sym->type == SYM_LOCAL ||
                   exp->id.sym->type == SYM_CONST);

            if(exp->id.sym->type != SYM_CONST) {
                assert(exp->id.sym->var.tag);

                exp->tag = exp->id.sym->var.tag;
            } else {
                exp->tag = exp->id.sym->constant.tag;    
            }
        } break;
        
        case EXP_CALL: {
            Symbol* func = ReferenceFunction(state, exp->call.calleeName);

            if(!func) {
                ReportErrorE(state, exp, "Calling undeclared function '%s'.\n", exp->call.calleeName);
            }

			if (func->type == SYM_FOREIGN_FUNCTION) {
				for (int i = 0; i < sb_count(exp->call.args); ++i) {
					ResolveTypes(state, exp->call.args[i]);

                    if(i >= sb_count(func->foreignFunc.argTags)) {
						if (!func->foreignFunc.varargs) {
							ReportErrorE(state, exp->call.args[i], "Too many arguments to function '%s' (%d expected).\n", exp->call.calleeName,
								sb_count(func->foreignFunc.argTags));
						}
                        break;
                    }

					const Symbol* argTag = func->foreignFunc.argTags[i];

					if (!CompareTags(exp->call.args[i]->tag, argTag)) {
						ReportErrorE(state, exp->call.args[i],
							"Argument %i is supposed to be a %s but you supplied a %s\n",
							i + 1, GetTagName(argTag),
							GetTagName(exp->call.args[i]->tag));
					}
				}

				exp->tag = func->foreignFunc.returnTag;
			} else {
				if (sb_count(exp->call.args) != sb_count(func->func.args)) {
					ReportErrorE(state, exp, "%s expects %d arguments but you supplied %d.\n",
						exp->call.calleeName, sb_count(func->func.args), sb_count(exp->call.args));
				}

				for (int i = 0; i < sb_count(exp->call.args); ++i) {
					ResolveTypes(state, exp->call.args[i]);

					const Symbol* argSym = func->func.args[i];

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
            switch(exp->binary.op) {
				case TINY_TOK_PLUS: case TINY_TOK_MINUS: case TINY_TOK_STAR: case TINY_TOK_SLASH: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

					bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
					bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

					bool fLhs = !iLhs && CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_FLOAT));
					bool fRhs = !iRhs && CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_FLOAT));

					if((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp, "Left and right hand side of binary op must be ints or floats, but they're %s and %s",
                                GetTagName(exp->binary.lhs->tag),
                                GetTagName(exp->binary.rhs->tag));
                    }
                    
					exp->tag = GetPrimTag((iLhs && iRhs) ? SYM_TAG_INT : SYM_TAG_FLOAT);
                } break;

				case TINY_TOK_AND: case TINY_TOK_OR: case TINY_TOK_PERCENT: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

					bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
					bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

					if (!(iLhs && iRhs)) {
						ReportErrorE(state, exp, "Both sides of binary op must be ints, but they're %s and %s\n",
							GetTagName(exp->binary.lhs->tag),
							GetTagName(exp->binary.rhs->tag));
					}

					exp->tag = GetPrimTag(SYM_TAG_INT);
				} break;

                case TINY_TOK_LOG_AND: case TINY_TOK_LOG_OR: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

                    if(!CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_BOOL)) ||
                       !CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_BOOL))) {
                        ReportErrorE(state, exp, "Left and right hand side of binary and/or must be bools, but they're %s and %s",
                                GetTagName(exp->binary.lhs->tag),
                                GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

				case TINY_TOK_GT: case TINY_TOK_LT: case TINY_TOK_LTE: case TINY_TOK_GTE: {                    
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

					bool iLhs = CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_INT));
					bool iRhs = CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_INT));

					bool fLhs = !iLhs && CompareTags(exp->binary.lhs->tag, GetPrimTag(SYM_TAG_FLOAT));
					bool fRhs = !iRhs && CompareTags(exp->binary.rhs->tag, GetPrimTag(SYM_TAG_FLOAT));

					if((!iLhs && !fLhs) || (!iRhs && !fRhs)) {
                        ReportErrorE(state, exp, "Left and right hand side of binary comparison must be ints or floats, but they're %s and %s",
                                GetTagName(exp->binary.lhs->tag),
                                GetTagName(exp->binary.rhs->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

				case TINY_TOK_EQUALS: case TINY_TOK_NOTEQUALS: {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

					if (exp->binary.lhs->tag->type == SYM_TAG_VOID ||
						exp->binary.rhs->tag->type == SYM_TAG_VOID) {
						ReportErrorE(state, exp, "Attempted to check for equality with void. This is not allowed.");
					}

					exp->tag = GetPrimTag(SYM_TAG_BOOL);
				} break;

                case TINY_TOK_DECLARE: {
                    assert(exp->binary.lhs->type == EXP_ID);

                    assert(exp->binary.lhs->id.sym);

                    ResolveTypes(state, exp->binary.rhs);

                    if(exp->binary.rhs->tag->type == SYM_TAG_VOID) {
                        ReportErrorE(state, exp, "Attempted to initialize variable with void expression. Don't do that.");
                    }

                    exp->binary.lhs->id.sym->var.tag = exp->binary.rhs->tag;

                    exp->tag = GetPrimTag(SYM_TAG_VOID);
                } break;

				case '=': {
                    ResolveTypes(state, exp->binary.lhs);
                    ResolveTypes(state, exp->binary.rhs);

					if (!CompareTags(exp->binary.lhs->tag, exp->binary.rhs->tag)) {
						ReportErrorE(state, exp, "Attempted to assign a %s to a %s", GetTagName(exp->binary.lhs->tag), GetTagName(exp->binary.rhs->tag));
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

            switch(exp->unary.op) {
                case TINY_TOK_MINUS: {
					bool i = CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_INT));
					bool f = !i && CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_FLOAT));

                    if(!(i || f)) {
                        ReportErrorE(state, exp, "Attempted to apply unary '-' to a %s.", GetTagName(exp->unary.exp->tag));
                    }

					exp->tag = i ? GetPrimTag(SYM_TAG_INT) : GetPrimTag(SYM_TAG_FLOAT);
                } break;

                case TINY_TOK_BANG: {
                    if(!CompareTags(exp->unary.exp->tag, GetPrimTag(SYM_TAG_BOOL))) {
                        ReportErrorE(state, exp, "Attempted to apply unary 'not' to a %s.", GetTagName(exp->unary.exp->tag));
                    }

                    exp->tag = GetPrimTag(SYM_TAG_BOOL);
                } break;

				default: assert(0); break;
            }
        } break;
           
        case EXP_BLOCK: {
            for(int i = 0; i < sb_count(exp->block); ++i) {
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

            if(!CompareTags(exp->ifx.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "If condition is supposed to be a bool but its a %s", GetTagName(exp->ifx.cond->tag));
            }

            ResolveTypes(state, exp->ifx.body);

            if(exp->ifx.alt) {
                ResolveTypes(state, exp->ifx.alt);
            }

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_RETURN: {
            if(exp->retExpr) {
                ResolveTypes(state, exp->retExpr);
            }

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_WHILE: {
            ResolveTypes(state, exp->whilex.cond);

            if(!CompareTags(exp->whilex.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "While condition is supposed to be a bool but its a %s", GetTagName(exp->whilex.cond->tag));
            }

            ResolveTypes(state, exp->whilex.body);

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;

        case EXP_FOR: {
            ResolveTypes(state, exp->forx.init);
            ResolveTypes(state, exp->forx.cond);

            if(!CompareTags(exp->forx.cond->tag, GetPrimTag(SYM_TAG_BOOL))) {
                ReportErrorE(state, exp, "For condition is supposed to be a bool but its a %s", GetTagName(exp->forx.cond->tag));
            }

            ResolveTypes(state, exp->forx.step);
            ResolveTypes(state, exp->forx.body);

            exp->tag = GetPrimTag(SYM_TAG_VOID);
        } break;
    }
}

static void CompileProgram(Tiny_State* state, Expr** program);

static void CompileGetId(Tiny_State* state, Expr* exp)
{
    assert(exp->type == EXP_ID);

    if (!exp->id.sym)
    {
        ReportErrorE(state, exp, "Referencing undeclared identifier '%s'.\n", exp->id.name);
    }

    assert(exp->id.sym->type == SYM_GLOBAL ||
        exp->id.sym->type == SYM_LOCAL ||
        exp->id.sym->type == SYM_CONST);

    if (exp->id.sym->type != SYM_CONST)
    {
        if (exp->id.sym->type == SYM_GLOBAL)
            GenerateCode(state, OP_GET);
        else if (exp->id.sym->type == SYM_LOCAL)
            GenerateCode(state, OP_GETLOCAL);

        GenerateInt(state, exp->id.sym->var.index);
    }
    else
    {
		if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_STR)) {
			GenerateCode(state, OP_PUSH_STRING);
			GenerateInt(state, exp->id.sym->constant.sIndex);
		} else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_BOOL)) {
			GenerateCode(state, exp->id.sym->constant.bValue ? OP_PUSH_TRUE : OP_PUSH_FALSE);
		} else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_INT)) {
			GenerateCode(state, OP_PUSH_INT);
			GenerateInt(state, exp->id.sym->constant.iValue);
		} else if (exp->id.sym->constant.tag == GetPrimTag(SYM_TAG_FLOAT)) {
			GenerateCode(state, OP_PUSH_FLOAT);
			GenerateInt(state, exp->id.sym->constant.fIndex);
		} else {
			assert(0);
		}
    }
}

static void CompileExpr(Tiny_State* state, Expr* exp);

static void CompileCall(Tiny_State* state, Expr* exp)
{
    assert(exp->type == EXP_CALL);

    for (int i = 0; i < sb_count(exp->call.args); ++i)
        CompileExpr(state, exp->call.args[i]);

    Symbol* sym = ReferenceFunction(state, exp->call.calleeName);
    if (!sym)
    {
        ReportErrorE(state, exp, "Attempted to call undefined function '%s'.\n", exp->call.calleeName);
    }

    if (sym->type == SYM_FOREIGN_FUNCTION)
    {
        GenerateCode(state, OP_CALLF);
        
        int nargs = sb_count(exp->call.args);
        int fNargs = sb_count(sym->foreignFunc.argTags);

        if(!(sym->foreignFunc.varargs && nargs >= fNargs) && fNargs != nargs) {
            ReportErrorE(state, exp, "Function '%s' expects %s%d args but you supplied %d.\n", exp->call.calleeName, sym->foreignFunc.varargs ? "at least " : "", fNargs, nargs);
        }

        GenerateInt(state, sb_count(exp->call.args));
        GenerateInt(state, sym->foreignFunc.index);
    }
    else
    {
        GenerateCode(state, OP_CALL);
        GenerateInt(state, sb_count(exp->call.args));
        GenerateInt(state, sym->func.index);
    }
}

static void CompileExpr(Tiny_State* state, Expr* exp)
{
    switch (exp->type)
    {
        case EXP_NULL:
        {
            GenerateCode(state, OP_PUSH_NULL);
        } break;

        case EXP_ID:
        {
            CompileGetId(state, exp);
        } break;

        case EXP_BOOL:
        {
            GenerateCode(state, exp->boolean ? OP_PUSH_TRUE : OP_PUSH_FALSE);
        } break;

		case EXP_INT: case EXP_CHAR:
        {
            GenerateCode(state, OP_PUSH_INT);
            GenerateInt(state, exp->iValue);
        } break;

		case EXP_FLOAT:
		{
            GenerateCode(state, OP_PUSH_FLOAT);
            GenerateInt(state, exp->fIndex);
		} break;

        case EXP_STRING:
        {
            GenerateCode(state, OP_PUSH_STRING);
            GenerateInt(state, exp->sIndex);
        } break;

        case EXP_CALL:
        {
            CompileCall(state, exp);
            GenerateCode(state, OP_GET_RETVAL);
        } break;

        case EXP_BINARY:
        {
            switch (exp->binary.op)
            {
                case TINY_TOK_PLUS:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_ADD);
                } break;

				case TINY_TOK_MINUS:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_SUB);
                } break;

                case TINY_TOK_STAR:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_MUL);
                } break;

				case TINY_TOK_SLASH:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_DIV);
                } break;

				case TINY_TOK_PERCENT:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_MOD);
                } break;

				case TINY_TOK_OR:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_OR);
                } break;

				case TINY_TOK_AND:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_AND);
                } break;

				case TINY_TOK_LT:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_LT);
                } break;

				case TINY_TOK_GT:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_GT);
                } break;

                case TINY_TOK_EQUALS:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_EQU);
                } break;

                case TINY_TOK_NOTEQUALS:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_EQU);
                    GenerateCode(state, OP_LOG_NOT);
                } break;

                case TINY_TOK_LTE:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_LTE);
                } break;

                case TINY_TOK_GTE:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_GTE);
                } break;

                case TINY_TOK_LOG_AND:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_LOG_AND);
                } break;

                case TINY_TOK_LOG_OR:
                {
                    CompileExpr(state, exp->binary.lhs);
                    CompileExpr(state, exp->binary.rhs);
                    GenerateCode(state, OP_LOG_OR);
                } break;

                default:
                    ReportErrorE(state, exp, "Found assignment when expecting expression.\n");
                    break;
            }
        } break;

        case EXP_PAREN:
        {
            CompileExpr(state, exp->paren);
        } break;

        case EXP_UNARY:
        {
            CompileExpr(state, exp->unary.exp);
            switch (exp->unary.op)
            {
                case TINY_TOK_MINUS:
                {
                    GenerateCode(state, OP_PUSH_INT);
                    GenerateInt(state, -1);
                    GenerateCode(state, OP_MUL);
                } break;

                case TINY_TOK_BANG:
                {
                    GenerateCode(state, OP_LOG_NOT);
                } break;

                default:
                    ReportErrorE(state, exp, "Unsupported unary operator %c (%d)\n", exp->unary.op, exp->unary.op);
                    break;
            }
        } break;

        default:
            ReportErrorE(state, exp, "Got statement when expecting expression.\n");
            break;
    }
}

static void CompileStatement(Tiny_State* state, Expr* exp)
{
    if(state->l.fileName) {
        GenerateCode(state, OP_FILE);
        GenerateInt(state, RegisterString(state->l.fileName));
    }

    GenerateCode(state, OP_POS);
    GenerateInt(state, exp->pos);

    switch(exp->type)
    {
        case EXP_CALL:
        {
            CompileCall(state, exp);
        } break;

        case EXP_BLOCK:
        {
            for(int i = 0; i < sb_count(exp->block); ++i) {
                CompileStatement(state, exp->block[i]);
            }
        } break;

        case EXP_BINARY:
        {    
            switch(exp->binary.op)
            {
                case TINY_TOK_DECLARECONST:
                    // Constants generate no code
                    break;
                
                case TINY_TOK_EQUAL: case TINY_TOK_DECLARE: // These two are handled identically in terms of code generated

                case TINY_TOK_PLUSEQUAL: case TINY_TOK_MINUSEQUAL: case TINY_TOK_STAREQUAL: case TINY_TOK_SLASHEQUAL:
				case TINY_TOK_PERCENTEQUAL: case TINY_TOK_ANDEQUAL: case TINY_TOK_OREQUAL:
                {
                    if (exp->binary.lhs->type == EXP_ID)
                    {
                        switch (exp->binary.op)
                        {
                            case TINY_TOK_PLUSEQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_ADD);
                            } break;

                            case TINY_TOK_MINUSEQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_SUB);
                            } break;

                            case TINY_TOK_STAREQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_MUL);
                            } break;

                            case TINY_TOK_SLASHEQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_DIV);
                            } break;

                            case TINY_TOK_PERCENTEQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_MOD);
                            } break;

                            case TINY_TOK_ANDEQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_AND);
                            } break;

                            case TINY_TOK_OREQUAL:
                            {
                                CompileGetId(state, exp->binary.lhs);
                                CompileExpr(state, exp->binary.rhs);
                                GenerateCode(state, OP_OR);
                            } break;

                            default:
                                CompileExpr(state, exp->binary.rhs);
                                break;
                        }

                        if (!exp->binary.lhs->id.sym)
                        {
                            // The variable being referenced doesn't exist
                            ReportErrorE(state, exp, "Assigning to undeclared identifier '%s'.\n", exp->binary.lhs->id.name);
                        }

                        if (exp->binary.lhs->id.sym->type == SYM_GLOBAL)
                            GenerateCode(state, OP_SET);
                        else if (exp->binary.lhs->id.sym->type == SYM_LOCAL)
                            GenerateCode(state, OP_SETLOCAL);
                        else        // Probably a constant, can't change it
                        {
                            ReportErrorE(state, exp, "Cannot assign to id '%s'.\n", exp->binary.lhs->id.name);
                        }

                        GenerateInt(state, exp->binary.lhs->id.sym->var.index);
                        exp->binary.lhs->id.sym->var.initialized = true;
                    }
                    else
                    {
                        ReportErrorE(state, exp, "LHS of assignment operation must be a variable\n");
                    }
                } break;

                default:
                    ReportErrorE(state, exp, "Invalid operation when expecting statement.\n");
                    break;
            }
        } break;
        
        case EXP_PROC:
        {
            GenerateCode(state, OP_GOTO);
            int skipGotoPc = sb_count(state->program);
            GenerateInt(state, 0);
            
            state->functionPcs[exp->proc.decl->func.index] = sb_count(state->program);
            
			for (int i = 0; i < sb_count(exp->proc.decl->func.locals); ++i) {
				GenerateCode(state, OP_PUSH_NULL);
			}
            
            if (exp->proc.body)
                CompileStatement(state, exp->proc.body);

            GenerateCode(state, OP_RETURN);
            GenerateIntAt(state, sb_count(state->program), skipGotoPc);
        } break;
        
        case EXP_IF:
        {
            CompileExpr(state, exp->ifx.cond);
            GenerateCode(state, OP_GOTOZ);
            
            int skipGotoPc = sb_count(state->program);
            GenerateInt(state, 0);
            
            if(exp->ifx.body)
                CompileStatement(state, exp->ifx.body);
            
            GenerateCode(state, OP_GOTO);
            int exitGotoPc = sb_count(state->program);
            GenerateInt(state, 0);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);

            if (exp->ifx.alt)
                CompileStatement(state, exp->ifx.alt);

            GenerateIntAt(state, sb_count(state->program), exitGotoPc);
        } break;
        
        case EXP_WHILE:
        {
            int condPc = sb_count(state->program);
            
            CompileExpr(state, exp->whilex.cond);
            
            GenerateCode(state, OP_GOTOZ);
            int skipGotoPc = sb_count(state->program);
            GenerateInt(state, 0);
            
            if(exp->whilex.body)
                CompileStatement(state, exp->whilex.body);
            
            GenerateCode(state, OP_GOTO);
            GenerateInt(state, condPc);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);
        } break;
        
        case EXP_FOR:
        {
            CompileStatement(state, exp->forx.init);

            int condPc = sb_count(state->program);
            CompileExpr(state, exp->forx.cond);

            GenerateCode(state, OP_GOTOZ);
            int skipGotoPc = sb_count(state->program);
            GenerateInt(state, 0);

            if (exp->forx.body)
                CompileStatement(state, exp->forx.body);

            CompileStatement(state, exp->forx.step);
            
            GenerateCode(state, OP_GOTO);
            GenerateInt(state, condPc);

            GenerateIntAt(state, sb_count(state->program), skipGotoPc);
        } break;

        case EXP_RETURN:
        {
            if(exp->retExpr)
            {
                CompileExpr(state, exp->retExpr);
                GenerateCode(state, OP_RETURN_VALUE);
            }
            else
                GenerateCode(state, OP_RETURN);
        } break;

        default:
            ReportErrorE(state, exp, "Got expression when expecting statement.\n");
            break;
    }
}

static void CompileProgram(Tiny_State* state, Expr** program)
{
    Expr** arr = program;
    for(int i = 0; i < sb_count(arr); ++i)  {
        CompileStatement(state, arr[i]);
    }
}

static void DeleteProgram(Expr** program);

static void Expr_destroy(Expr* exp)
{
    switch(exp->type)
    {
        case EXP_ID: 
        {
            free(exp->id.name);
        } break;

		case EXP_NULL: case EXP_BOOL: case EXP_CHAR: case EXP_INT: case EXP_FLOAT: case EXP_STRING: break;
        
        case EXP_CALL: 
        {
            free(exp->call.calleeName);
            for(int i = 0; i < sb_count(exp->call.args); ++i)
                Expr_destroy(exp->call.args[i]);

            sb_free(exp->call.args);
        } break;

        case EXP_BLOCK:
        {
            for(int i = 0; i < sb_count(exp->block); ++i) {
                Expr_destroy(exp->block[i]);
            }

            sb_free(exp->block);
        } break;
        
        case EXP_BINARY: Expr_destroy(exp->binary.lhs); Expr_destroy(exp->binary.rhs); break;
        case EXP_PAREN: Expr_destroy(exp->paren); break;
        
        case EXP_PROC: 
        {
            if (exp->proc.body) 
                Expr_destroy(exp->proc.body);
        } break;

        case EXP_IF: Expr_destroy(exp->ifx.cond); if (exp->ifx.body) Expr_destroy(exp->ifx.body); if (exp->ifx.alt) Expr_destroy(exp->ifx.alt); break;
        case EXP_WHILE: Expr_destroy(exp->whilex.cond); if(exp->whilex.body) Expr_destroy(exp->whilex.body); break;
        case EXP_RETURN: if(exp->retExpr) Expr_destroy(exp->retExpr); break;
        case EXP_UNARY: Expr_destroy(exp->unary.exp); break;

        case EXP_FOR:
        {
            Expr_destroy(exp->forx.init);
            Expr_destroy(exp->forx.cond);
            Expr_destroy(exp->forx.step);

            Expr_destroy(exp->forx.body);
        } break;

        default: assert(false); break;
    }
    free(exp);
}

void DeleteProgram(Expr** program)
{
    Expr** arr = program;
    for(int i = 0; i < sb_count(program); ++i) {
        Expr_destroy(arr[i]);
    }

    sb_free(program);
}

static void CheckInitialized(Tiny_State* state)
{
    const char* fmt = "Attempted to use uninitialized variable '%s'.\n";

    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* node = state->globalSymbols[i];

        assert(node->type != SYM_LOCAL);

        if (node->type == SYM_GLOBAL)
        {
            if (!node->var.initialized)
            {
                ReportErrorS(state, node, fmt, node->name);
            }
        }
        else if (node->type == SYM_FUNCTION)
        {
            // Only check locals, arguments are initialized implicitly
            for(int i = 0; i < sb_count(node->func.locals); ++i) {
                Symbol* local = node->func.locals[i];

                assert(local->type == SYM_LOCAL);

                if (!local->var.initialized)
                {
                    ReportErrorS(state, local, fmt, local->name);
                }
            }
        }
    }
}

// Goes through the registered symbols (GlobalSymbols) and assigns all foreign
// functions to their respective index in ForeignFunctions
static void BuildForeignFunctions(Tiny_State* state)
{
    for(int i = 0; i < sb_count(state->globalSymbols); ++i) {
        Symbol* node = state->globalSymbols[i];

        if (node->type == SYM_FOREIGN_FUNCTION)
            state->foreignFunctions[node->foreignFunc.index] = node->foreignFunc.callee;
    }
}

static void CompileState(Tiny_State* state, Expr** prog)
{
    // If this state was already compiled and it ends with an OP_HALT, We'll just overwrite it
    if(sb_count(state->program) > 0) {
        if(state->program[sb_count(state->program) - 1] == OP_HALT) {
            stb__sbn(state->program) -= 1;
        }
    }
    
    // Allocate room for vm execution info

    // We realloc because this state might be compiled multiple times (if, e.g., Tiny_CompileString is called twice with same state)
    if (state->numFunctions > 0) {
        state->functionPcs = realloc(state->functionPcs, state->numFunctions * sizeof(int));
    }

    if (state->numForeignFunctions > 0) {
        state->foreignFunctions = realloc(state->foreignFunctions, state->numForeignFunctions * sizeof(Tiny_ForeignFunction));
    }
    
    assert(state->numForeignFunctions == 0 || state->foreignFunctions);
    assert(state->numFunctions == 0 || state->functionPcs);

    BuildForeignFunctions(state);

    for(int i = 0; i < sb_count(prog); ++i) {
        ResolveTypes(state, prog[i]);
    }

    CompileProgram(state, prog);
    GenerateCode(state, OP_HALT);

    CheckInitialized(state);        // Done after compilation because it might have registered undefined functions during the compilation stage
    
}

void Tiny_CompileString(Tiny_State* state, const char* name, const char* string)
{
    Tiny_InitLexer(&state->l, name, string);

    CurTok = 0;
    Expr** prog = ParseProgram(state); 
        
    CompileState(state, prog);

    Tiny_DestroyLexer(&state->l);

    DeleteProgram(prog);
}

void Tiny_CompileFile(Tiny_State* state, const char* filename)
{
    FILE* file = fopen(filename, "rb");

    if(!file)
    {
        fprintf(stderr, "Error: Unable to open file '%s' for reading\n", filename);
        exit(1);
    }

	fseek(file, 0, SEEK_END);

	long len = ftell(file);
	
	char* s = malloc(len + 1);

	rewind(file);

	fread(s, 1, len, file);
	s[len] = 0;

	fclose(file);

	Tiny_CompileString(state, filename, s);

	free(s);
}

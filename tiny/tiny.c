// tiny.c -- an bytecode-based interpreter for the tiny language
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#include "tiny.h"
#include "tiny_detail.h"

const Tiny_Value Tiny_Null = { TINY_VAL_NULL };

static int NumNumbers = 0;
static double Numbers[MAX_NUMBERS];

static int NumStrings = 0;
static char Strings[MAX_STRINGS][MAX_TOK_LEN] = { 0 };

void* emalloc(size_t size)
{
	void* data = malloc(size);
	assert(data && "Out of memory!");
	return data;
}

void* erealloc(void* mem, size_t newSize)
{
	void* newMem = realloc(mem, newSize);
	assert(newMem && "Out of memory!");
	return newMem;
}

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
		if (obj->nat.prop && obj->nat.prop->free)
			obj->nat.prop->free(obj->nat.addr);
	}

	free(obj);
}

static inline bool IsObject(Tiny_Value val)
{
	return val.type == TINY_VAL_STRING || val.type == TINY_VAL_NATIVE;
}

void Tiny_Mark(Tiny_Value value)
{
    if(!IsObject(value))
        return;

    Tiny_Object* obj = value.obj;

    assert(obj);	
	
	if(obj->marked) return;
	
	if(obj->type == TINY_VAL_NATIVE)
	{
		if(obj->nat.prop && obj->nat.prop->mark)
			obj->nat.prop->mark(obj->nat.addr);
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

Tiny_Value Tiny_NewNative(Tiny_StateThread* thread, void* ptr, const Tiny_NativeProp* prop)
{
	Tiny_Object* obj = NewObject(thread, TINY_VAL_NATIVE);
	
	obj->nat.addr = ptr;
	obj->nat.prop = prop;

	Tiny_Value val;

	val.type = TINY_VAL_NATIVE;
	val.obj = obj;

	return val;
}

Tiny_ValueType Tiny_GetType(const Tiny_Value val)
{
	return val.type;
}

bool Tiny_GetBool(const Tiny_Value val, bool* pbool)
{
	if (val.type != TINY_VAL_BOOL) return false;

	*pbool = val.boolean;
	return true;
}

bool Tiny_GetNum(const Tiny_Value val, double* pnum)
{
	if (val.type != TINY_VAL_NUM) return false;

	*pnum = val.number;
	return true;
}

bool Tiny_GetString(const Tiny_Value val, const char** pstr)
{
	if (val.type != TINY_VAL_STRING) return false;

	*pstr = val.obj->string;
	return true;
}

bool Tiny_GetAddr(const Tiny_Value val, void** paddr)
{
	if (val.type != TINY_VAL_NATIVE) return false;

	*paddr = val.obj->nat.addr;
	return true;
}

bool Tiny_GetProp(const Tiny_Value val, const Tiny_NativeProp** pprop)
{
	if (val.type != TINY_VAL_NATIVE) return false;

	*pprop = val.obj->nat.prop;
	return true;
}

bool Tiny_ExpectBool(const Tiny_Value val)
{
	assert(val.type == TINY_VAL_BOOL);
	return val.boolean;
}

double Tiny_ExpectNum(const Tiny_Value val)
{
	assert(val.type == TINY_VAL_NUM);
	return val.number;
}

const char* Tiny_ExpectString(const Tiny_Value val)
{
	assert(val.type == TINY_VAL_STRING);
	return val.obj->string;
}

void* Tiny_ExpectAddr(const Tiny_Value val)
{
	assert(val.type == TINY_VAL_NATIVE);
	return val.obj->nat.addr;
}

const Tiny_NativeProp* Tiny_ExpectProp(const Tiny_Value val)
{
	assert(val.type == TINY_VAL_NATIVE);
	return val.obj->nat.prop;
}

Tiny_Value Tiny_NewBool(bool value)
{
	Tiny_Value val;

	val.type = TINY_VAL_BOOL;
	val.boolean = value;

	return val;
}

Tiny_Value Tiny_NewNumber(double value)
{
	Tiny_Value val;

	val.type = TINY_VAL_NUM;
	val.number = value;

	return val;
}

Tiny_Value Tiny_NewString(Tiny_StateThread* thread, char* string)
{
	Tiny_Object* obj = NewObject(thread, TINY_VAL_STRING);
	obj->string = string;

	Tiny_Value val;

	val.type = TINY_VAL_STRING;
	val.obj = obj;

	return val;
}

static void Symbol_destroy(Symbol* sym);

Tiny_State* Tiny_CreateState(void)
{
    Tiny_State* state = emalloc(sizeof(Tiny_State));

    state->programLength = 0;

    state->numGlobalVars = 0;
    
    state->numFunctions = 0;
    state->functionPcs = NULL;

    state->numForeignFunctions = 0;
    state->foreignFunctions = NULL;

    state->currScope = 0;
    state->currFunc = NULL;
    state->globalSymbols = NULL;

    state->fileName = NULL;
    state->lineNumber = 0;

    return state;
}

void Tiny_DeleteState(Tiny_State* state)
{
	// Delete all symbols
	while (state->globalSymbols)
	{
		Symbol* next = state->globalSymbols->next;

		Symbol_destroy(state->globalSymbols);
		state->globalSymbols = next;
	}

    // Reset function and variable data
	free(state->functionPcs);
	free(state->foreignFunctions);
}

void MarkAll(Tiny_StateThread* thread)
{
    assert(thread->state);

    Tiny_Mark(thread->retVal);

	for (int i = 0; i < thread->sp; ++i)
        Tiny_Mark(thread->stack[i]);

    for (int i = 0; i < thread->state->numGlobalVars; ++i)
        Tiny_Mark(thread->globalVars[i]);
}

static void GenerateCode(Tiny_State* state, Word inst)
{	
	assert(state->programLength < MAX_PROG_LEN && "Program Overflow!");
    state->program[state->programLength++] = inst;
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

static int RegisterNumber(double value)
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
    strcpy(Strings[NumStrings++], string);

    return NumStrings - 1; 
}

static Symbol* Symbol_create(SymbolType type, const char* name)
{
	Symbol* sym = emalloc(sizeof(Symbol));

	sym->name = estrdup(name);
	sym->type = type;
	sym->next = NULL;

	return sym;
}

static void Symbol_destroy(Symbol* sym)
{
	if (sym->type == SYM_FUNCTION)
	{
		Symbol* arg = sym->func.argsHead;

		while (arg)
		{
			assert(arg->type == SYM_LOCAL);

			Symbol* next = arg->next;

			Symbol_destroy(arg);

			arg = next;
		}
	
		Symbol* local = sym->func.localsHead;

		while (local)
		{
			assert(local->type == SYM_LOCAL);

			Symbol* next = local->next;

			Symbol_destroy(local);

			local = next;
		}
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
		Symbol* node = state->currFunc->func.localsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			if (node->var.scope == state->currScope)
				node->var.scopeEnded = true;

			node = node->next;
		}
	}

	--state->currScope;
}

static Symbol* ReferenceVariable(Tiny_State* state, const char* name)
{
	if (state->currFunc)
	{
		// Check local variables
		Symbol* node = state->currFunc->func.localsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			// Make sure that it's available in the current scope too
			if (!node->var.scopeEnded && strcmp(node->name, name) == 0)
				return node;

			node = node->next;
		}

		// Check arguments
		node = state->currFunc->func.argsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			if (strcmp(node->name, name) == 0)
				return node;

			node = node->next;
		}
	}

	// Check global variables/constants
	Symbol* node = state->globalSymbols;

	while (node)
	{
		if (node->type == SYM_GLOBAL || node->type == SYM_CONST)
		{
			if (strcmp(node->name, name) == 0)
				return node;
		}

		node = node->next;
	}

	// This variable doesn't exist
	return NULL;
}

static Symbol* DeclareGlobalVar(Tiny_State* state, const char* name)
{
	Symbol* node = ReferenceVariable(state, name);

	while (node)
	{
		if ((node->type == SYM_GLOBAL || node->type == SYM_CONST) && strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "Attempted to declare multiple global entities with the same name '%s'.", name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_GLOBAL, name);

	newNode->var.initialized = false;
	newNode->var.index = state->numGlobalVars;
	newNode->var.scope = 0;					// Global variable scope don't matter
	newNode->var.scopeEnded = true;

	newNode->next = state->globalSymbols;
	state->globalSymbols = newNode;

	state->numGlobalVars += 1;

	return newNode;
}

static Symbol* DeclareArgument(Tiny_State* state, const char* name)
{
	assert(state->currFunc);

	Symbol* node = state->currFunc->func.argsHead;

	while (node)
	{
		assert(node->type == SYM_LOCAL);

		if (strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "Function '%s' takes multiple arguments with the same name '%s'.\n", state->currFunc->name, name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_LOCAL, name);

	newNode->var.initialized = false;
	newNode->var.scopeEnded = false;
	newNode->var.index = -(state->currFunc->func.nargs + 1);
	newNode->var.scope = 0;								// These should be accessible anywhere in the function

	newNode->next = state->currFunc->func.argsHead;
	state->currFunc->func.argsHead = newNode;

	state->currFunc->func.nargs += 1;
	
	return newNode;
}

static Symbol* DeclareLocal(Tiny_State* state, const char* name)
{
	assert(state->currFunc);

	Symbol* node = state->currFunc->func.localsHead;

	while (node)
	{
		assert(node->type == SYM_LOCAL);

		if (!node->var.scopeEnded && strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "Attempted to declare multiple local variables in the same scope '%s' with the same name '%s'.\n", state->currFunc->name, name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_LOCAL, name);

	newNode->var.initialized = false;
	newNode->var.scopeEnded = false;
	newNode->var.index = state->currFunc->func.nlocals;
	newNode->var.scope = state->currScope;

	newNode->next = state->currFunc->func.localsHead;
	state->currFunc->func.localsHead = newNode;

	state->currFunc->func.nlocals += 1;

	return newNode;
}

static Symbol* DeclareConst(Tiny_State* state, const char* name, bool isString, int index)
{
	Symbol* node = ReferenceVariable(state, name);

	if (node && (node->type != SYM_LOCAL && node->type != SYM_FUNCTION && node->type != SYM_FOREIGN_FUNCTION))
	{
		fprintf(stderr, "Attempted to define constant with the same name '%s' as another value.\n", name);
		exit(1);
	}

	if (state->currFunc)
		fprintf(stderr, "Warning: Constant '%s' declared inside function bodies will still have global scope.\n", name);
	
	Symbol* newNode = Symbol_create(SYM_CONST, name);

	newNode->constant.index = index;
    newNode->constant.isString = isString;

	newNode->next = state->globalSymbols;
	state->globalSymbols = newNode;

	return node;
}

static Symbol* DeclareFunction(Tiny_State* state, const char* name)
{
	Symbol* newNode = Symbol_create(SYM_FUNCTION, name);

	newNode->func.index = state->numFunctions;
	newNode->func.nargs = newNode->func.nlocals = 0;
	newNode->func.argsHead = NULL;
	newNode->func.localsHead = NULL;

	newNode->next = state->globalSymbols;
	state->globalSymbols = newNode;

	state->numFunctions += 1;

	return newNode;
}

static Symbol* ReferenceFunction(Tiny_State* state, const char* name)
{
	Symbol* node = state->globalSymbols;

	while (node)
	{
		if ((node->type == SYM_FUNCTION || node->type == SYM_FOREIGN_FUNCTION) &&
			strcmp(node->name, name) == 0)
			return node;

		node = node->next;
	}

	return NULL;
}

static void ExecuteCycle(Tiny_StateThread* thread);
static void DoPushIndir(Tiny_StateThread* thread, int nargs);

/*static void CallProc(int id, int nargs)
{
	if(id < 0) return;
	
	DoPushIndir(nargs);
	ProgramCounter = FunctionPcs[id];
	
	while(ProgramCounter < state->programLength && ProgramCounter >= 0)
		ExecuteCycle();
}*/

void Tiny_BindForeignFunction(Tiny_State* state, Tiny_ForeignFunction func, const char* name)
{
	Symbol* node = state->globalSymbols;

	while (node)
	{
		if (node->type == SYM_FOREIGN_FUNCTION && strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "There is already a foreign function bound to name '%s'.", name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_FOREIGN_FUNCTION, name);

	newNode->foreignFunc.index = state->numForeignFunctions;
	newNode->foreignFunc.callee = func;

	newNode->next = state->globalSymbols;
	state->globalSymbols = newNode;

	state->numForeignFunctions += 1;
}

void Tiny_BindConstNumber(Tiny_State* state, const char* name, double number)
{
	DeclareConst(state, name, false, RegisterNumber(number));
}

void Tiny_BindConstString(Tiny_State* state, const char* name, const char* string)
{
	DeclareConst(state, name, true, RegisterString(string));
}

enum
{
	OP_PUSH_NULL,
	OP_PUSH_TRUE,
	OP_PUSH_FALSE,

	OP_PUSH_NUMBER,
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

	OP_HALT
};

static int ReadInteger(Tiny_StateThread* thread)
{
    assert(thread->state);

	int val = 0;
	Word* wp = (Word*)(&val);
	for(int i = 0; i < 4; ++i)
	{
		*wp = thread->state->program[thread->pc++];
		++wp;
	}

	return val;
}

inline void DoPush(Tiny_StateThread* thread, Tiny_Value value)
{
	if(thread->sp >= MAX_STACK) 
	{
		fprintf(stderr, "Stack Overflow at PC: %i! (Stack size: %i)", thread->pc, thread->sp);
		exit(1);
	}

	thread->stack[thread->sp++] = value;
}

inline Tiny_Value DoPop(Tiny_StateThread* thread)
{
    assert(thread->state);

	if(thread->sp <= 0) 
	{
		fprintf(stderr, "Stack Underflow at PC: %i (Inst %i)!", thread->pc, thread->state->program[thread->pc]);
		exit(1);
	}

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

void DoPushIndir(Tiny_StateThread* thread, int nargs)
{
	thread->indirStack[thread->indirStackSize++] = nargs;
	thread->indirStack[thread->indirStackSize++] = thread->fp;
	thread->indirStack[thread->indirStackSize++] = thread->pc;

	thread->fp = thread->sp;
}

void DoPopIndir(Tiny_StateThread* thread)
{
    thread->sp = thread->fp;

	int prevPc = thread->indirStack[--thread->indirStackSize];
	int prevFp = thread->indirStack[--thread->indirStackSize];
	int nargs = thread->indirStack[--thread->indirStackSize];
	
	thread->sp -= nargs;
	thread->fp = prevFp;
	thread->pc = prevPc;
}

void ExecuteCycle(Tiny_StateThread* thread)
{
    assert(thread->state);
    
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

        case OP_PUSH_NUMBER:
		{
			++thread->pc;
            
            int numberIndex = ReadInteger(thread);

            DoPush(thread, Tiny_NewNumber(Numbers[numberIndex]));
		} break;
		
		case OP_POP:
		{
			DoPop(thread);
			++thread->pc;
		} break;
		
		#define BIN_OP(OP, operator) case OP_##OP: { Tiny_Value val2 = DoPop(thread); Tiny_Value val1 = DoPop(thread); DoPush(thread, Tiny_NewNumber(val1.number operator val2.number)); ++thread->pc; } break;
		#define BIN_OP_INT(OP, operator) case OP_##OP: { Tiny_Value val2 = DoPop(thread); Tiny_Value val1 = DoPop(thread); DoPush(thread, Tiny_NewNumber((int)val1.number operator (int)val2.number)); ++thread->pc; } break;

		#define REL_OP(OP, operator) case OP_##OP: { Tiny_Value val2 = DoPop(thread); Tiny_Value val1 = DoPop(thread); DoPush(thread, Tiny_NewBool(val1.number operator val2.number)); ++thread->pc; } break;

		case OP_MUL:
		{
			Tiny_Value val2 = DoPop(thread);
			Tiny_Value val1 = DoPop(thread);

			DoPush(thread, Tiny_NewNumber(val1.number * val2.number));

			++thread->pc;
		} break;

		BIN_OP(ADD, +)
		BIN_OP(SUB, -)
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

			if (a.type != b.type)
				DoPush(thread, Tiny_NewBool(false));
			else
			{
				if (a.type == TINY_VAL_NULL)
					DoPush(thread, Tiny_NewBool(true));
				else if (a.type == TINY_VAL_BOOL)
					DoPush(thread, Tiny_NewBool(a.boolean == b.boolean));
				else if (a.type == TINY_VAL_NUM)
					DoPush(thread, Tiny_NewBool(a.number == b.number));
				else if (a.type == TINY_VAL_STRING)
					DoPush(thread, Tiny_NewBool(strcmp(a.obj->string, b.obj->string) == 0));
				else if (a.type == TINY_VAL_NATIVE)
					DoPush(thread, Tiny_NewBool(a.obj->nat.addr == b.obj->nat.addr));
			}
		} break;

		case OP_LOG_NOT:
		{
			++thread->pc;
			Tiny_Value a = DoPop(thread);

			DoPush(thread, Tiny_NewBool(!Tiny_ExpectBool(a)));
		} break;

		case OP_LOG_AND:
		{
			++thread->pc;
			Tiny_Value b = DoPop(thread);
			Tiny_Value a = DoPop(thread);

			DoPush(thread, Tiny_NewBool(Tiny_ExpectBool(a) && Tiny_ExpectBool(b)));
		} break;

		case OP_LOG_OR:
		{
			++thread->pc;
			Tiny_Value b = DoPop(thread);
			Tiny_Value a = DoPop(thread);

			DoPush(thread, Tiny_NewBool(Tiny_ExpectBool(a) || Tiny_ExpectBool(b)));
		} break;

		case OP_PRINT:
		{
			Tiny_Value val = DoPop(thread);
			if(val.type == TINY_VAL_NUM) printf("%g\n", val.number);
			else if (val.obj->type == TINY_VAL_STRING) printf("%s\n", val.obj->string);
			else if (val.obj->type == TINY_VAL_NATIVE) printf("<native at %p>\n", val.obj->nat.addr);
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

			if(!Tiny_ExpectBool(val))
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

            thread->retVal = state->foreignFunctions[fIdx](&thread->stack[prevSize], nargs);
			
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
			thread->pc = -1;
		} break;
	}

	// Only collect garbage in between iterations
	if (thread->numObjects >= thread->maxNumObjects)
		GarbageCollect(thread);
}

enum
{
	TOK_BEGIN = -1,
	TOK_END = -2,
	TOK_IDENT = -3,
	TOK_DECLARE = -4,		// :=
	TOK_DECLARECONST = -5,	// ::
	TOK_PLUSEQUAL = -6,		// +=
	TOK_MINUSEQUAL = -7,	// -=
	TOK_MULEQUAL = -8,		// *=
	TOK_DIVEQUAL = -9,		// /=
	TOK_MODEQUAL = -10,		// %=
	TOK_OREQUAL	= -11,		// |=
	TOK_ANDEQUAL = -12,		// &=
	TOK_NUMBER = -13,
	TOK_STRING = -14,
	TOK_PROC = -15,
	TOK_IF = -16,
	TOK_EQUALS = -17,
	TOK_NOTEQUALS = -18,
	TOK_LTE = -19,
	TOK_GTE = -20,
	TOK_RETURN = -21,
	TOK_WHILE = -22,
	TOK_FOR = -23,
	TOK_DO = -24,
	TOK_THEN = -25,
	TOK_ELSE = -26,
	TOK_EOF = -27,
	TOK_NOT = -28,
	TOK_AND = -29,
	TOK_OR = -30,
	TOK_NULL = -31,
	TOK_TRUE = -32,
	TOK_FALSE = -33
};

char TokenBuffer[MAX_TOK_LEN];
double TokenNumber;

int Peek(FILE* in)
{
	int c = getc(in);
	ungetc(c, in);
	return c;
}

int GetToken(Tiny_State* state, FILE* in)
{
	static int last = ' ';
	while (isspace(last))
	{
		if (last == '\n')
			++state->lineNumber;

		last = getc(in);
	}

	if(isalpha(last))
	{
		int i = 0;
		while(isalnum(last) || last == '_')
		{
			assert(i < MAX_TOK_LEN - 1 && "Token was too long!");
			TokenBuffer[i++] = last;
			last = getc(in);
		}
		TokenBuffer[i] = '\0';
		
		if (strcmp(TokenBuffer, "begin") == 0) return TOK_BEGIN;
		if (strcmp(TokenBuffer, "end") == 0) return TOK_END;
		if (strcmp(TokenBuffer, "func") == 0) return TOK_PROC;
		if (strcmp(TokenBuffer, "if") == 0) return TOK_IF;
		if (strcmp(TokenBuffer, "return") == 0) return TOK_RETURN;
		if (strcmp(TokenBuffer, "while") == 0) return TOK_WHILE;
		if (strcmp(TokenBuffer, "then") == 0) return TOK_THEN;
		if (strcmp(TokenBuffer, "for") == 0) return TOK_FOR;
		if (strcmp(TokenBuffer, "do") == 0) return TOK_DO;
		if (strcmp(TokenBuffer, "else") == 0) return TOK_ELSE;
		if (strcmp(TokenBuffer, "not") == 0) return TOK_NOT;
		if (strcmp(TokenBuffer, "and") == 0) return TOK_AND;
		if (strcmp(TokenBuffer, "or") == 0) return TOK_OR;
		if (strcmp(TokenBuffer, "null") == 0) return TOK_NULL;
		if (strcmp(TokenBuffer, "true") == 0) return TOK_TRUE;
		if (strcmp(TokenBuffer, "false") == 0) return TOK_FALSE;
		
		return TOK_IDENT;
	}
	
	if(isdigit(last))
	{
		int i = 0;
		while(isdigit(last) || last == '.')
		{
			assert(i < MAX_TOK_LEN - 1 && "Number was too long!");
			TokenBuffer[i++] = last;
			last = getc(in);
		}
		TokenBuffer[i] = '\0';
		
		TokenNumber = strtod(TokenBuffer, NULL);
		return TOK_NUMBER;
	}
	
	if(last == '#')
	{
		while(last != '\n' && last != EOF) last = getc(in);
		return GetToken(state, in);
	}

	if(last == '"')
	{
		last = getc(in);
		int i = 0;
		while(last != '"')
		{
			if (last == '\\')
			{
				last = getc(in);

				switch (last)
				{
					case 'n': last = '\n'; break;
					case 'r': last = '\r'; break;
					case 't': last = '\t'; break;
					case 'b': last = '\b'; break;
					case 'a': last = '\a'; break;
					case 'v': last = '\v'; break;
					case 'f': last = '\f'; break;
					case '\\': last = '\\'; break;
					case '"': last = '"'; break;
					default:
						if (isdigit(last)) // Octal number
						{
							int n1 = last - '0';
							last = getc(in);

							if (!isdigit(last))
							{
								fprintf(stderr, "Expected three digits in octal escape sequence but only got one.\n");
								exit(1);
							}

							int n2 = last - '0';
							last = getc(in);

							if (!isdigit(last))
							{
								fprintf(stderr, "Expected three digits in octal escape sequence but only got two.\n");
								exit(1);
							}

							int n3 = last - '0';
							last = n3 + n2 * 8 + n1 * 8 * 8;
						}
						else
						{
							fprintf(stderr, "Unsupported escape sequence '\\%c'.\n", last);
							exit(1);
						}
						break;
				}
			}

			TokenBuffer[i++] = last;
			last = getc(in);
		}
		TokenBuffer[i] = '\0';
		
		last = getc(in);
		return TOK_STRING;
	}
	
	if(last == EOF)
		return TOK_EOF;
	
	if(last == '=')
	{
		if(Peek(in) == '=')
		{
			last = getc(in);
			last = getc(in);
			return TOK_EQUALS;
		}
	}
	
	if(last == '!')
	{
		if(Peek(in) == '=')
		{
			last = getc(in);
			last = getc(in);
			return TOK_NOTEQUALS;
		}
	}
	
	if(last == '<')
	{
		if(Peek(in) == '=')
		{
			last = getc(in);
			last = getc(in);
			return TOK_LTE;
		}
	}
	
	if(last == '>')
	{
		if(Peek(in) == '=')
		{
			last = getc(in);
			last = getc(in);
			return TOK_GTE;
		}
	}

	if (last == ':')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_DECLARE;
		}
		else if (Peek(in) == ':')
		{
			getc(in);
			last = getc(in);
			return TOK_DECLARECONST;
		}
	}

	if (last == '+')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_PLUSEQUAL;
		}
	}
	
	if (last == '-')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_MINUSEQUAL;
		}
	}

	if (last == '*')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_MULEQUAL;
		}
	}

	if (last == '/')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_DIVEQUAL;
		}
	}

	if (last == '%')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_MODEQUAL;
		}
	}

	if (last == '&')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_ANDEQUAL;
		}
	}

	if (last == '|')
	{
		if (Peek(in) == '=')
		{
			getc(in);
			last = getc(in);
			return TOK_OREQUAL;
		}
	}

	int lastChar = last;
	last = getc(in);
	return lastChar;
}

typedef enum
{
	EXP_ID,
	EXP_CALL,
	EXP_NULL,
	EXP_BOOL,
	EXP_NUM,
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
	struct sExpr* next;

	union
	{
		bool boolean;

		int number;
		int string;

		struct
		{
			char* name;
			Symbol* sym;
		} id;

		struct
		{
			char* calleeName;
			struct sExpr* args[MAX_ARGS];
			int numArgs;
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
		
		struct
		{
			struct sExpr* exprHead;
		} block;

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

Expr* Expr_create(ExprType type)
{
	Expr* exp = emalloc(sizeof(Expr));
	exp->type = type;
	exp->next = NULL;
	return exp;
}

int CurTok;

int GetNextToken(Tiny_State* state, FILE* in)
{
	CurTok = GetToken(state, in);
	return CurTok;
}

Expr* ParseExpr(Tiny_State* state, FILE* in);

static void ExpectToken(const Tiny_State* state, int tok, const char* msg)
{
	if (CurTok != tok)
	{
		fprintf(stderr, "%s(%i):", state->fileName, state->lineNumber);
		fprintf(stderr, "%s", msg);
		putc('\n', stderr);

		exit(1);
	}
}

static Expr* ParseIf(Tiny_State* state, FILE* in)
{
	Expr* exp = Expr_create(EXP_IF);

	GetNextToken(state, in);

	exp->ifx.cond = ParseExpr(state, in);
	exp->ifx.body = ParseExpr(state, in);

	if (CurTok == TOK_ELSE)
	{
		GetNextToken(state, in);
		exp->ifx.alt = ParseExpr(state, in);
	}
	else
		exp->ifx.alt = NULL;

	return exp;
}

Expr* ParseFactor(Tiny_State* state, FILE* in)
{
	switch(CurTok)
	{
		case TOK_NULL:
		{
			Expr* exp = Expr_create(EXP_NULL);

			GetNextToken(state, in);

			return exp;
		} break;

		case TOK_TRUE:
		case TOK_FALSE:
		{
			Expr* exp = Expr_create(EXP_BOOL);

			exp->boolean = CurTok == TOK_TRUE;

			GetNextToken(state, in);

			return exp;
		} break;

		case '{':
		{
			GetNextToken(state, in);

			OpenScope(state);

			Expr* curExp = ParseExpr(state, in);
			Expr* head = curExp;

			while (CurTok != '}')
			{
				curExp->next = ParseExpr(state, in);
				curExp = curExp->next;
			}
			GetNextToken(state, in);

			CloseScope(state);

			Expr* exp = Expr_create(EXP_BLOCK);

			exp->block.exprHead = head;

			return exp;
		} break;

		case TOK_IDENT:
		{
			char* ident = estrdup(TokenBuffer);
			GetNextToken(state, in);
			if(CurTok != '(')
			{
				Expr* exp;
				
				exp = Expr_create(EXP_ID);
				
				exp->id.sym = ReferenceVariable(state, ident);
				exp->id.name = ident;

				return exp;
			}
			
			Expr* exp = Expr_create(EXP_CALL);
			
			GetNextToken(state, in);
			exp->call.numArgs = 0;
			
			while(CurTok != ')')
			{
				if (exp->call.numArgs >= MAX_ARGS)
				{
					fprintf(stderr, "Exceeded maximum number of arguments in call expression (%d).\n", MAX_ARGS);
					exit(1);
				}

				exp->call.args[exp->call.numArgs++] = ParseExpr(state, in);

				if(CurTok == ',') GetNextToken(state, in);
				else if(CurTok != ')')
				{
					fprintf(stderr, "Expected ')' after attempted call to proc %s\n", ident);
					exit(1);
				}
			}

			exp->call.calleeName = ident;

			GetNextToken(state, in);
			return exp;
		} break;
		
		case '-': case '+': case TOK_NOT:
		{
			int op = CurTok;
			GetNextToken(state, in);
			Expr* exp = Expr_create(EXP_UNARY);
			exp->unary.op = op;
			exp->unary.exp = ParseFactor(state, in);

			return exp;
		} break;
		
		case TOK_NUMBER:
		{
			Expr* exp = Expr_create(EXP_NUM);
			exp->number = RegisterNumber(TokenNumber);
			GetNextToken(state, in);
			return exp;
		} break;

		case TOK_STRING:
		{
			Expr* exp = Expr_create(EXP_STRING);
			exp->string = RegisterString(TokenBuffer);
			GetNextToken(state, in);
			return exp;
		} break;
		
		case TOK_PROC:
		{
			if(state->currFunc)
			{
				fprintf(stderr, "Attempted to define function inside function '%s'. This is not allowed.\n", state->currFunc->name);
				exit(1);
			}
			
			Expr* exp = Expr_create(EXP_PROC);
			
			GetNextToken(state, in);

			ExpectToken(state, TOK_IDENT, "Function name must be identifier!");
			
			exp->proc.decl = DeclareFunction(state, TokenBuffer);
			state->currFunc = exp->proc.decl;

			GetNextToken(state, in);
			
			ExpectToken(state, '(', "Expected '(' after function name");
			GetNextToken(state, in);
			
			while(CurTok != ')')
			{
				ExpectToken(state, TOK_IDENT, "Expected identifier in function parameter list");

				DeclareArgument(state, TokenBuffer);
				GetNextToken(state, in);
				
				if (CurTok != ')' && CurTok != ',')
				{
					fprintf(stderr, "Expected ')' or ',' after parameter name in function parameter list");
					exit(1);
				}

				if(CurTok == ',') GetNextToken(state, in);
			}
			
			GetNextToken(state, in);

			OpenScope(state);
			
			exp->proc.body = ParseExpr(state, in);

			CloseScope(state);

			state->currFunc = NULL;

			return exp;
		} break;
		
		case TOK_IF:
		{
			return ParseIf(state, in);
		} break;
		
		case TOK_WHILE:
		{
			GetNextToken(state, in);
			Expr* exp = Expr_create(EXP_WHILE);

			exp->whilex.cond = ParseExpr(state, in);

			OpenScope(state);
			
			exp->whilex.body = ParseExpr(state, in);
			
			CloseScope(state);


			return exp;
		} break;

		case TOK_FOR:
		{
			GetNextToken(state, in);
			Expr* exp = Expr_create(EXP_FOR);
			
			// Every local declared after this is scoped to the for
			OpenScope(state);

			exp->forx.init = ParseExpr(state, in);

			ExpectToken(state, ';', "Expected ';' after for initializer.");

			GetNextToken(state, in);

			exp->forx.cond = ParseExpr(state, in);

			ExpectToken(state, ';', "Expected ';' after for condition.");

			GetNextToken(state, in);

			exp->forx.step = ParseExpr(state, in);

			exp->forx.body = ParseExpr(state, in);

			CloseScope(state);

			return exp;
		} break;
		
		case TOK_RETURN:
		{
			GetNextToken(state, in);
			Expr* exp = Expr_create(EXP_RETURN);
			if(CurTok == ';')
			{
				GetNextToken(state, in);	
				exp->retExpr = NULL;
				return exp;
			}

			exp->retExpr = ParseExpr(state, in);
			return exp;
		} break;

		case '(':
		{
			GetNextToken(state, in);
			Expr* inner = ParseExpr(state, in);
			assert(CurTok == ')' && "Expected matching ')' after previous '('");
			GetNextToken(state, in);
			
			Expr* exp = Expr_create(EXP_PAREN);
			exp->paren = inner;
			return exp;
		} break;
		
		default: break;
	}

	fprintf(stderr, "%s(%i): Unexpected token %i (%c)\n", state->fileName, state->lineNumber, CurTok, CurTok);
	exit(1);
}

int GetTokenPrec()
{
	int prec = -1;
	switch(CurTok)
	{
		case '*': case '/': case '%': case '&': case '|': prec = 5; break;
		
		case '+': case '-':				prec = 4; break;
		
		case TOK_LTE: case TOK_GTE:
		case TOK_EQUALS: case TOK_NOTEQUALS:
		case '<': case '>':				prec = 3; break;
		
		case TOK_AND: case TOK_OR:		prec = 2; break;

		case TOK_PLUSEQUAL: case TOK_MINUSEQUAL: case TOK_MULEQUAL: case TOK_DIVEQUAL:
		case TOK_MODEQUAL: case TOK_ANDEQUAL: case TOK_OREQUAL:
		case TOK_DECLARECONST:
		case TOK_DECLARE: case '=':						prec = 1; break;
	}
	
	return prec;
}

Expr* ParseBinRhs(Tiny_State* state, FILE* in, int exprPrec, Expr* lhs)
{
	while(1)
	{
		int prec = GetTokenPrec();
		
		if(prec < exprPrec)
			return lhs;

		int binOp = CurTok;

		// They're trying to declare a variable (we can only know this when we 
		// encounter this token)
		if (binOp == TOK_DECLARE)
		{
			if (lhs->type != EXP_ID)
			{
				fprintf(stderr, "Expected identifier to the left-hand side of ':='.\n");
				exit(1);
			}

			// If we're inside a function declare a local, otherwise a global
			if (state->currFunc)
				lhs->id.sym = DeclareLocal(state, lhs->id.name);
			else
				lhs->id.sym = DeclareGlobalVar(state, lhs->id.name);
		}

		GetNextToken(state, in);

		Expr* rhs = ParseFactor(state, in);
		int nextPrec = GetTokenPrec();
		
		if(prec < nextPrec)
			rhs = ParseBinRhs(state, in, prec + 1, rhs);

		if (binOp == TOK_DECLARECONST)
		{
			if (lhs->type != EXP_ID)
			{
				fprintf(stderr, "Expected identifier to the left-hand side of '::'.\n");
				exit(1);
			}

			if (rhs->type == EXP_NUM)
				DeclareConst(state, lhs->id.name, false, rhs->number);
			else if (rhs->type == EXP_STRING)
				DeclareConst(state, lhs->id.name, true, rhs->string);
			else
			{
				fprintf(stderr, "Expected number or string to be bound to constant '%s'.\n", lhs->id.name);
				exit(1);
			}
		}

		Expr* newLhs = Expr_create(EXP_BINARY);
		
		newLhs->binary.lhs = lhs;
		newLhs->binary.rhs = rhs;
		newLhs->binary.op = binOp;
		
		lhs = newLhs;
	}
}

Expr* ParseExpr(Tiny_State* state, FILE* in)
{
	Expr* factor = ParseFactor(state, in);
	return ParseBinRhs(state, in, 0, factor);
}

Expr* ParseProgram(Tiny_State* state, FILE* in)
{
	GetNextToken(state, in);
		
	if(CurTok != TOK_EOF)
	{
		Expr* head = ParseExpr(state, in);
		Expr* exp = head;
		
		while(CurTok != TOK_EOF)
		{
			Expr* stmt = ParseExpr(state, in);
			head->next = stmt;
			head = stmt;
		}
		return exp;
	}
	return NULL;
}

void PrintProgram(Tiny_State* state, Expr* program);

void PrintExpr(Tiny_State* state, Expr* exp)
{
	switch(exp->type)
	{
		case EXP_BLOCK:
		{
			Expr* node = exp->block.exprHead;
			printf("{\n");

			while (node)
			{
				PrintExpr(state, exp);
				node = node->next;
			}

			printf("\n}\n");
		} break;

		case EXP_ID:
		{	
			printf("%s", exp->id.name);
		} break;
		
		case EXP_CALL:
		{
			printf("%s(", exp->call.calleeName);
			for(int i = 0; i < exp->call.numArgs; ++i)
			{
				PrintExpr(state, exp->call.args[i]);
				if(i + 1 < exp->call.numArgs) printf(",");
			}
			printf(")");
		} break;
		
		case EXP_NUM:
		{
			printf("%g", Numbers[exp->number]);
		} break;

		case EXP_STRING:
		{
			printf("%s", Strings[exp->string]);
		} break;	
		
		case EXP_BINARY:
		{
			printf("(");
			PrintExpr(state, exp->binary.lhs);
			printf(" %c ", exp->binary.op);
			PrintExpr(state, exp->binary.rhs);
			printf(")");
		} break;
		
		case EXP_PAREN:
		{
			printf("(");
			PrintExpr(state, exp->paren);
			printf(")");
		} break;
		
		case EXP_UNARY:
		{
			printf("%c", exp->unary.op);
			PrintExpr(state, exp->unary.exp);
		} break;
		
		case EXP_PROC:
		{
			printf("func %s\n", exp->proc.decl->name);
			if (exp->proc.body)
				PrintExpr(state, exp->proc.body);

			printf("end\n");
		} break;
		
		case EXP_IF:
		{
			printf("if ");
			PrintExpr(state, exp->ifx.cond);
			if (exp->ifx.body)
				PrintExpr(state, exp->ifx.body);

			if (exp->ifx.alt)
			{
				printf("else\n");
				PrintProgram(state, exp->ifx.alt);
			}

			printf("end\n");
		} break;
		
		case EXP_WHILE:
		{
			printf("while ");
			PrintExpr(state, exp->whilex.cond);
			if (exp->whilex.body)
				PrintExpr(state, exp->ifx.body);
			printf("end\n");
		} break;
		
		case EXP_RETURN:
		{
			printf("return ");
			if(exp->retExpr)
				PrintExpr(state, exp->retExpr);
		} break;
		
		default:
		{
			printf("cannot print expression type %i\n", exp->type);
		} break;
	}
}

void PrintProgram(Tiny_State* state, Expr* program)
{
	Expr* exp = program;
	printf("begin\n");
	while(exp)
	{
		PrintExpr(state, exp);
		exp = exp->next;
	}
	printf("\nend\n");
}

void CompileProgram(Tiny_State* state, Expr* program);

static void CompileGetId(Tiny_State* state, Expr* exp)
{
	assert(exp->type == EXP_ID);

	if (!exp->id.sym)
	{
		fprintf(stderr, "Referencing undeclared identifier '%s'.\n", exp->id.name);
		exit(1);
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
        if(exp->id.sym->constant.isString)
            GenerateCode(state, OP_PUSH_STRING);
        else
            GenerateCode(state, OP_PUSH_NUMBER);
		GenerateInt(state, exp->id.sym->constant.index);
	}
}

static void CompileExpr(Tiny_State* state, Expr* exp);

static void CompileCall(Tiny_State* state, Expr* exp)
{
	assert(exp->type == EXP_CALL);

	for (int i = 0; i < exp->call.numArgs; ++i)
		CompileExpr(state, exp->call.args[i]);

	Symbol* sym = ReferenceFunction(state, exp->call.calleeName);
	if (!sym)
	{
		fprintf(stderr, "Attempted to call undefined function '%s'.\n", exp->call.calleeName);
		exit(1);
	}

	if (sym->type == SYM_FOREIGN_FUNCTION)
	{
		GenerateCode(state, OP_CALLF);
		GenerateInt(state, exp->call.numArgs);
		GenerateInt(state, sym->foreignFunc.index);
	}
	else
	{
		GenerateCode(state, OP_CALL);
		GenerateInt(state, exp->call.numArgs);
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

		case EXP_NUM:
		{
			GenerateCode(state, OP_PUSH_NUMBER);
			GenerateInt(state, exp->number);
		} break;

		case EXP_STRING:
		{
			GenerateCode(state, OP_PUSH_STRING);
			GenerateInt(state, exp->string);
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
				case '+':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_ADD);
				} break;

				case '*':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_MUL);
				} break;

				case '/':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_DIV);
				} break;

				case '%':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_MOD);
				} break;

				case '|':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_OR);
				} break;

				case '&':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_AND);
				} break;

				case '-':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_SUB);
				} break;

				case '<':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_LT);
				} break;

				case '>':
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_GT);
				} break;


				case TOK_EQUALS:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_EQU);
				} break;

				case TOK_NOTEQUALS:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_EQU);
					GenerateCode(state, OP_LOG_NOT);
				} break;

				case TOK_LTE:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_LTE);
				} break;

				case TOK_GTE:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_GTE);
				} break;

				case TOK_AND:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_LOG_AND);
				} break;

				case TOK_OR:
				{
					CompileExpr(state, exp->binary.lhs);
					CompileExpr(state, exp->binary.rhs);
					GenerateCode(state, OP_LOG_OR);
				} break;

				default:
					fprintf(stderr, "Found assignment when expecting expression.\n");
					exit(1);
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
				case '-':
				{
					GenerateCode(state, OP_PUSH_NUMBER);
					GenerateInt(state, RegisterNumber(-1));
					GenerateCode(state, OP_MUL);
				} break;

				case TOK_NOT:
				{
					GenerateCode(state, OP_LOG_NOT);
				} break;

				default:
					fprintf(stderr, "Unsupported unary operator %c (%d)\n", exp->unary.op, exp->unary.op);
					exit(1);
					break;
			}
		} break;
	}
}

static void CompileStatement(Tiny_State* state, Expr* exp)
{
	switch(exp->type)
	{
		case EXP_CALL:
		{
			CompileCall(state, exp);
		} break;

		case EXP_BLOCK:
		{
			Expr* node = exp->block.exprHead;

			while (node)
			{
				CompileStatement(state, node);
				node = node->next;
			}
		} break;

		case EXP_BINARY:
		{	
			switch(exp->binary.op)
			{
				case TOK_DECLARECONST:
					// Constants generate no code
					break;
				
				case '=': case TOK_DECLARE: // These two are handled identically in terms of code generated

				case TOK_PLUSEQUAL: case TOK_MINUSEQUAL: case TOK_MULEQUAL: case TOK_DIVEQUAL:
				case TOK_MODEQUAL: case TOK_ANDEQUAL: case TOK_OREQUAL:
				{
					if (exp->binary.lhs->type == EXP_ID)
					{
						switch (exp->binary.op)
						{
							case TOK_PLUSEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_ADD);
							} break;

							case TOK_MINUSEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_SUB);
							} break;

							case TOK_MULEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_MUL);
							} break;

							case TOK_DIVEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_DIV);
							} break;

							case TOK_MODEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_MOD);
							} break;

							case TOK_ANDEQUAL:
							{
								CompileGetId(state, exp->binary.lhs);
								CompileExpr(state, exp->binary.rhs);
								GenerateCode(state, OP_AND);
							} break;

							case TOK_OREQUAL:
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
							fprintf(stderr, "Assigning to undeclared identifier '%s'.\n", exp->binary.lhs->id.name);
							exit(1);
						}

						if (exp->binary.lhs->id.sym->type == SYM_GLOBAL)
							GenerateCode(state, OP_SET);
						else if (exp->binary.lhs->id.sym->type == SYM_LOCAL)
							GenerateCode(state, OP_SETLOCAL);
						else		// Probably a constant, can't change it
						{
							fprintf(stderr, "Cannot assign to id '%s'.\n", exp->binary.lhs->id.name);
							exit(1);
						}

						GenerateInt(state, exp->binary.lhs->id.sym->var.index);
						exp->binary.lhs->id.sym->var.initialized = true;
					}
					else
					{
						fprintf(stderr, "LHS of assignment operation must be a variable\n");
						exit(1);
					}
				} break;

				default:
					fprintf(stderr, "Invalid operation when expecting statement.\n");
					exit(1);
					break;
			}
		} break;
		
		case EXP_PROC:
		{
			GenerateCode(state, OP_GOTO);
			int skipGotoPc = state->programLength;
			GenerateInt(state, 0);
			
			state->functionPcs[exp->proc.decl->func.index] = state->programLength;
			
			for(int i = 0; i < exp->proc.decl->func.nlocals; ++i)
			{
				GenerateCode(state, OP_PUSH_NUMBER);
				GenerateInt(state, RegisterNumber(0));
			}
			
			if (exp->proc.body)
				CompileStatement(state, exp->proc.body);

			GenerateCode(state, OP_RETURN);
			GenerateIntAt(state, state->programLength, skipGotoPc);
		} break;
		
		case EXP_IF:
		{
			CompileExpr(state, exp->ifx.cond);
			GenerateCode(state, OP_GOTOZ);
			
			int skipGotoPc = state->programLength;
			GenerateInt(state, 0);
			
			if(exp->ifx.body)
				CompileStatement(state, exp->ifx.body);
			
			GenerateCode(state, OP_GOTO);
			int exitGotoPc = state->programLength;
			GenerateInt(state, 0);

			GenerateIntAt(state, state->programLength, skipGotoPc);

			if (exp->ifx.alt)
				CompileStatement(state, exp->ifx.alt);

			GenerateIntAt(state, state->programLength, exitGotoPc);
		} break;
		
		case EXP_WHILE:
		{
			int condPc = state->programLength;
			
			CompileExpr(state, exp->whilex.cond);
			
			GenerateCode(state, OP_GOTOZ);
			int skipGotoPc = state->programLength;
			GenerateInt(state, 0);
			
			if(exp->whilex.body)
				CompileStatement(state, exp->whilex.body);
			
			GenerateCode(state, OP_GOTO);
			GenerateInt(state, condPc);

			GenerateIntAt(state, state->programLength, skipGotoPc);
		} break;
		
		case EXP_FOR:
		{
			CompileStatement(state, exp->forx.init);

			int condPc = state->programLength;
			CompileExpr(state, exp->forx.cond);

			GenerateCode(state, OP_GOTOZ);
			int skipGotoPc = state->programLength;
			GenerateInt(state, 0);

			if (exp->forx.body)
				CompileStatement(state, exp->forx.body);

			CompileStatement(state, exp->forx.step);
			
			GenerateCode(state, OP_GOTO);
			GenerateInt(state, condPc);

			GenerateIntAt(state, state->programLength, skipGotoPc);
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
	}
}

void CompileProgram(Tiny_State* state, Expr* program)
{
	Expr* exp = program;
	while(exp)
	{
		CompileStatement(state, exp);
		exp = exp->next;
	}
}

void DeleteProgram(Expr* program);

void Expr_destroy(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID: 
		{
			free(exp->id.name);
		} break;

		case EXP_NULL: case EXP_BOOL: case EXP_NUM: case EXP_STRING: break;
		
		case EXP_CALL: 
		{
			free(exp->call.calleeName);
			for(int i = 0; i < exp->call.numArgs; ++i)
				Expr_destroy(exp->call.args[i]);
		} break;

		case EXP_BLOCK:
		{
			Expr* node = exp->block.exprHead;

			while (node)
			{
				Expr* next = node->next;
				Expr_destroy(node);
				node = next;
			}
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

void DeleteProgram(Expr* program)
{
	Expr* exp = program;
	while(exp)
	{
		Expr* next = exp->next;
		Expr_destroy(exp);
		exp = next;
	}
}

static void DebugMachineProgram(Tiny_State* state)
{
	for(int i = 0; i < state->programLength; ++i)
	{
		switch(state->program[i])
		{
            case OP_PUSH_NUMBER:	printf("push_number\n"); break;
            case OP_PUSH_STRING:    printf("push_string\n"); break;
			case OP_POP:			printf("pop\n"); break;
			case OP_ADD:			printf("add\n"); break;
			case OP_SUB:			printf("sub\n"); break;
			case OP_MUL:			printf("mul\n"); break;
			case OP_DIV:			printf("div\n"); break;
			case OP_EQU:			printf("equ\n"); break;
			case OP_LOG_NOT:		printf("log_not\n"); break;
			case OP_LT:				printf("lt\n"); break;
			case OP_LTE:			printf("lte\n"); break;
			case OP_GT:				printf("gt\n"); break;
			case OP_GTE:			printf("gte\n"); break;
			case OP_PRINT:			printf("print\n"); break;
			case OP_SET:			printf("set\n"); i += 4; break;
			case OP_GET:			printf("get\n"); i += 4; break;
			case OP_READ:			printf("read\n"); break;
			case OP_GOTO:			printf("goto\n"); i += 4; break;
			case OP_GOTOZ:			printf("gotoz\n"); i += 4; break;
			case OP_CALL:			printf("call\n"); i += 8; break;
			case OP_RETURN:			printf("return\n"); break;
			case OP_RETURN_VALUE:	printf("return_value\n"); break;
			case OP_GETLOCAL:		printf("getlocal\n"); i += 4; break;
			case OP_SETLOCAL:		printf("setlocal\n"); i += 4; break;
			case OP_HALT:			printf("halt\n");
		}
	}
}

static void CheckInitialized(Tiny_State* state)
{
	Symbol* node = state->globalSymbols;

	bool error = false;
	const char* fmt = "Attempted to use uninitialized variable '%s'.\n";

	while(node)
	{
		assert(node->type != SYM_LOCAL);

		if (node->type == SYM_GLOBAL)
		{
			if (!node->var.initialized)
			{
				fprintf(stderr, fmt, node->name);
				error = true;
			}
		}
		else if (node->type == SYM_FUNCTION)
		{
			// Only check locals, arguments are initialized implicitly
			Symbol* local = node->func.localsHead;

			while (local)
			{
				assert(local->type == SYM_LOCAL);

				if (!local->var.initialized)
				{
					fprintf(stderr, fmt, local->name);
					error = true;
				}

				local = local->next;
			}
		}

		node = node->next;
	}

	if (error)
		exit(1);
}

// Goes through the registered symbols (GlobalSymbols) and assigns all foreign
// functions to their respective index in ForeignFunctions
static void BuildForeignFunctions(Tiny_State* state)
{
	Symbol* node = state->globalSymbols;

	while (node)
	{
		if (node->type == SYM_FOREIGN_FUNCTION)
		    state->foreignFunctions[node->foreignFunc.index] = node->foreignFunc.callee;

		node = node->next;
	}
}

void Tiny_CompileFile(Tiny_State* state, const char* filename)
{
    FILE* file = fopen(filename, "r");

    if(!file)
    {
        fprintf(stderr, "Error: Unable to open file '%s' for reading\n", filename);
        exit(1);
    }

	state->lineNumber = 1;
	state->fileName = filename;

	CurTok = 0;
	Expr* prog = ParseProgram(state, file);

	// Allocate room for vm execution info
    state->functionPcs = calloc(state->numFunctions, sizeof(int));
	state->foreignFunctions = calloc(state->numForeignFunctions, sizeof(Tiny_ForeignFunction));

    assert(state->functionPcs && state->foreignFunctions);

	BuildForeignFunctions(state);

	CompileProgram(state, prog);
	GenerateCode(state, OP_HALT);

	CheckInitialized(state);		// Done after compilation because it might have registered undefined functions during the compilation stage
	
	DeleteProgram(prog);
}



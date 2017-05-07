// tiny -- an bytecode-based interpreter for the tiny language
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#include "tiny.h"

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

#define MAX_PROG_LEN	2048
#define MAX_CONST_AMT	256
#define MAX_STACK		1024
#define MAX_INDIR		1024
#define MAX_VARS		128
#define MAX_FUNCS		128
#define MAX_ARGS		32

typedef unsigned char Word;

Word Program[MAX_PROG_LEN];
int ProgramLength;
int ProgramCounter;
int FramePointer;

Object* GCHead;
int NumObjects;
int MaxNumObjects;

void DeleteObject(Object* obj)
{
	if(obj->type == OBJ_STRING) free(obj->string);
	if (obj->type == OBJ_NATIVE)
	{
		if (obj->nat.prop && obj->nat.prop->free)
			obj->nat.prop->free(obj->nat.addr);
	}

	free(obj);
}

void Mark(Object* obj)
{
	if(!obj) 
	{
		printf("attempted to mark null object\n");
		return;
	}
	
	if(obj->marked) return;
	
	if(obj->type == OBJ_NATIVE)
	{
		if(obj->nat.prop && obj->nat.prop->mark)
			obj->nat.prop->mark(obj->nat.addr);
	}

	obj->marked = 1;
}

void MarkAll();

void Sweep()
{
	Object** object = &GCHead;
	while(*object)
	{
		if(!(*object)->marked)
		{
			Object* unreached = *object;
			--NumObjects;
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

void GarbageCollect()
{
	MarkAll();
	Sweep();
	MaxNumObjects = NumObjects * 2;
}

Object* NewObject(ObjectType type)
{
	Object* obj = emalloc(sizeof(Object));
	
	obj->type = type;
	obj->next = GCHead;
	GCHead = obj;
	obj->marked = 0;
	
	NumObjects++;
	
	return obj;
}

Value NewNative(void* ptr, const NativeProp* prop)
{
	Object* obj = NewObject(OBJ_NATIVE);
	
	obj->nat.addr = ptr;
	obj->nat.prop = prop;

	Value val;

	val.type = VAL_OBJ;
	val.obj = obj;

	return val;
}

Value NewNumber(double value)
{
	Value val;

	val.type = VAL_NUM;
	val.number = value;

	return val;
}

Value NewString(char* string)
{
	Object* obj = NewObject(OBJ_STRING);
	obj->string = string;

	Value val;

	val.type = VAL_OBJ;
	val.obj = obj;

	return val;
}

typedef enum
{
	SYM_GLOBAL,
	SYM_LOCAL,
	SYM_CONST,
	SYM_FUNCTION,
	SYM_FOREIGN_FUNCTION
} SymbolType;

typedef struct sSymbol
{
	struct sSymbol* next;
	SymbolType type;
	char* name;

	union
	{
		struct
		{
			bool initialized;	// Has the variable been assigned to?
			bool scopeEnded;	// If true, then this variable cannot be accessed anymore
			int scope, index;
		} var; // Used for both local and global

		struct
		{
			int index;			// Numbers and strings map to unique indices into the Constants array so
								// there's no need to distinguish between them at the symbol level
		} constant;

		struct
		{
			int index;
			int nargs, nlocals;

			struct sSymbol* argsHead;
			struct sSymbol* localsHead;
		} func;

		struct
		{
			ForeignFunction callee;
			int index;
		} foreignFunc;
	};
} Symbol;

Value RetVal;

Value Stack[MAX_STACK];
int StackSize = 0;

int IndirStack[MAX_INDIR];
int IndirStackSize;

int NumGlobalVars = 0;
Value* GlobalVars = NULL;

int NumFunctions = 0;
int* FunctionPcs = NULL;

int NumForeignFunctions = 0;
ForeignFunction* ForeignFunctions = NULL;

int CurrScope = 0;
Symbol* CurrFunc = NULL;
Symbol* GlobalSymbols = NULL;

const char* FileName = NULL;
int LineNumber = 1;

typedef enum
{
	CST_NUM,
	CST_STR
} ConstType;

typedef struct
{
	ConstType type;
	int index;
	union
	{
		char* string;
		double number;
	};
} ConstInfo;

ConstInfo* Constants;
int ConstantCapacity = 1;
int ConstantAmount = 0;

ConstInfo* NewConst(ConstType type)
{
	if(ConstantAmount + 1 >= ConstantCapacity)
	{
		ConstantCapacity *= 2;
		Constants = erealloc(Constants, ConstantCapacity * sizeof(ConstInfo));
	}

	ConstInfo* c = &Constants[ConstantAmount++];
	
	c->type = type;
	c->index = ConstantAmount - 1;

	return c;
}

static void Symbol_destroy(Symbol* sym);

void ResetCompiler(void)
{
	// Reset position
	FileName = "unknown";
	LineNumber = 0;

	// Delete all symbols
	while (GlobalSymbols)
	{
		Symbol* next = GlobalSymbols->next;

		Symbol_destroy(GlobalSymbols);
		GlobalSymbols = next;
	}

	// Destroy and reset constants
	for (int i = 0; i < ConstantAmount; ++i)
	{
		if (Constants[i].type == CST_STR)
			free(Constants[i].string);
	}

	free(Constants);
	Constants = NULL;

	ConstantAmount = 0;
	ConstantCapacity = 1;

	// Reset scope
	CurrFunc = NULL;
	CurrScope = 0;

	// Reset function and variable data
	NumGlobalVars = 0;
	free(GlobalVars);

	NumFunctions = 0;
	free(FunctionPcs);

	NumForeignFunctions = 0;
	free(ForeignFunctions);
}

void MarkAll()
{
	if (RetVal.type == VAL_OBJ)
		Mark(RetVal.obj);

	for (int i = 0; i < StackSize; ++i)
	{
		if (Stack[i].type == VAL_OBJ)
			Mark(Stack[i].obj);
	}

	for (int i = 0; i < NumGlobalVars; ++i)
	{
		if (GlobalVars[i].type == VAL_OBJ)
			Mark(GlobalVars[i].obj);
	}
}

void ResetMachine()
{
	RetVal.type = VAL_NUM;
	RetVal.number = -1;

	StackSize = 0;
	FramePointer = 0;
	IndirStackSize = 0;
	
	while (GCHead)
	{
		Object* next = GCHead->next;
		DeleteObject(GCHead);
		GCHead = next;
	}

	NumObjects = 0;
	MaxNumObjects = 8;
	ProgramCounter = -1;
}

static void GenerateCode(Word inst)
{	
	assert(ProgramLength < MAX_PROG_LEN && "Program Overflow!");
	Program[ProgramLength++] = inst;
}

static void GenerateInt(int value)
{
	Word* wp = (Word*)(&value);
	for(int i = 0; i < 4; ++i)
		GenerateCode(*wp++);
}

static void GenerateIntAt(int value, int pc)
{
	Word* wp = (Word*)(&value);
	for(int i = 0; i < 4; ++i)
		Program[pc + i] = *wp++;
}

static int RegisterNumber(double value)
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		if(Constants[i].type == CST_NUM)
		{
			if(Constants[i].number == value)
				return i;
		}
	}
	
	ConstInfo* num = NewConst(CST_NUM);
	
	num->number = value;

	return num->index;
}

static int RegisterString(const char* string)
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		if(Constants[i].type == CST_STR)
		{
			if(strcmp(Constants[i].string, string) == 0)
				return i;
		}
	}
	
	ConstInfo* str = NewConst(CST_STR);
	
	str->string = estrdup(string);
	
	return str->index;
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

static void OpenScope()
{
	++CurrScope;
}

static void CloseScope()
{
	if (CurrFunc)
	{
		Symbol* node = CurrFunc->func.localsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			if (node->var.scope == CurrScope)
				node->var.scopeEnded = true;

			node = node->next;
		}
	}

	--CurrScope;
}

static Symbol* ReferenceVariable(const char* name)
{
	if (CurrFunc)
	{
		// Check local variables
		Symbol* node = CurrFunc->func.localsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			// Make sure that it's available in the current scope too
			if (!node->var.scopeEnded && strcmp(node->name, name) == 0)
				return node;

			node = node->next;
		}

		// Check arguments
		node = CurrFunc->func.argsHead;

		while (node)
		{
			assert(node->type == SYM_LOCAL);

			if (strcmp(node->name, name) == 0)
				return node;

			node = node->next;
		}
	}

	// Check global variables/constants
	Symbol* node = GlobalSymbols;

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

static Symbol* DeclareGlobalVar(const char* name)
{
	Symbol* node = ReferenceVariable(name);

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
	newNode->var.index = NumGlobalVars;
	newNode->var.scope = 0;					// Global variable scope don't matter
	newNode->var.scopeEnded = true;

	newNode->next = GlobalSymbols;
	GlobalSymbols = newNode;

	NumGlobalVars += 1;

	return newNode;
}

static Symbol* DeclareArgument(const char* name)
{
	assert(CurrFunc);

	Symbol* node = CurrFunc->func.argsHead;

	while (node)
	{
		assert(node->type == SYM_LOCAL);

		if (strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "Function '%s' takes multiple arguments with the same name '%s'.\n", CurrFunc->name, name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_LOCAL, name);

	newNode->var.initialized = false;
	newNode->var.scopeEnded = false;
	newNode->var.index = -(CurrFunc->func.nargs + 1);
	newNode->var.scope = 0;								// These should be accessible anywhere in the function

	newNode->next = CurrFunc->func.argsHead;
	CurrFunc->func.argsHead = newNode;

	CurrFunc->func.nargs += 1;
	
	return newNode;
}

static Symbol* DeclareLocal(const char* name)
{
	assert(CurrFunc);

	Symbol* node = CurrFunc->func.localsHead;

	while (node)
	{
		assert(node->type == SYM_LOCAL);

		if (!node->var.scopeEnded && strcmp(node->name, name) == 0)
		{
			fprintf(stderr, "Attempted to declare multiple local variables in the same scope '%s' with the same name '%s'.\n", CurrFunc->name, name);
			exit(1);
		}

		node = node->next;
	}

	Symbol* newNode = Symbol_create(SYM_LOCAL, name);

	newNode->var.initialized = false;
	newNode->var.scopeEnded = false;
	newNode->var.index = CurrFunc->func.nlocals;
	newNode->var.scope = CurrScope;

	newNode->next = CurrFunc->func.localsHead;
	CurrFunc->func.localsHead = newNode;

	CurrFunc->func.nlocals += 1;

	return newNode;
}

static Symbol* DeclareConst(const char* name, int index)
{
	Symbol* node = ReferenceVariable(name);

	if (node && (node->type != SYM_LOCAL && node->type != SYM_FUNCTION && node->type != SYM_FOREIGN_FUNCTION))
	{
		fprintf(stderr, "Attempted to define constant with the same name '%s' as another value.\n", name);
		exit(1);
	}

	if (CurrFunc)
		fprintf(stderr, "Warning: Constant '%s' declared inside function bodies will still have global scope.\n", name);
	
	Symbol* newNode = Symbol_create(SYM_CONST, name);

	newNode->constant.index = index;

	newNode->next = GlobalSymbols;
	GlobalSymbols = newNode;

	return node;
}

static Symbol* DeclareFunction(const char* name)
{
	Symbol* newNode = Symbol_create(SYM_FUNCTION, name);

	newNode->func.index = NumFunctions;
	newNode->func.nargs = newNode->func.nlocals = 0;
	newNode->func.argsHead = NULL;
	newNode->func.localsHead = NULL;

	newNode->next = GlobalSymbols;
	GlobalSymbols = newNode;

	NumFunctions += 1;

	return newNode;
}

static Symbol* ReferenceFunction(const char* name)
{
	Symbol* node = GlobalSymbols;

	while (node)
	{
		if ((node->type == SYM_FUNCTION || node->type == SYM_FOREIGN_FUNCTION) &&
			strcmp(node->name, name) == 0)
			return node;

		node = node->next;
	}

	return NULL;
}

int GetProcId(const char* name)
{
	Symbol* node = GlobalSymbols;

	while (node)
	{
		if (node->type == SYM_FUNCTION && strcmp(node->name, name) == 0)
			return node->func.index;
	}

	return -1;
}

void ExecuteCycle(void);
void DoPushIndir(int nargs);

void CallProc(int id, int nargs)
{
	if(id < 0) return;
	
	DoPushIndir(nargs);
	ProgramCounter = FunctionPcs[id];
	
	while(ProgramCounter < ProgramLength && ProgramCounter >= 0)
		ExecuteCycle();
}

void BindForeignFunction(ForeignFunction func, const char* name)
{
	Symbol* node = GlobalSymbols;

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

	newNode->foreignFunc.index = NumForeignFunctions;
	newNode->foreignFunc.callee = func;

	newNode->next = GlobalSymbols;
	GlobalSymbols = newNode;

	NumForeignFunctions += 1;
}

void DefineConstNumber(const char* name, double number)
{
	DeclareConst(name, RegisterNumber(number));
}

void DefineConstString(const char* name, const char* string)
{
	DeclareConst(name, RegisterString(string));
}

enum
{
	OP_PUSH,
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

int ReadInteger()
{
	int val = 0;
	Word* wp = (Word*)(&val);
	for(int i = 0; i < 4; ++i)
	{
		*wp = Program[ProgramCounter++];
		++wp;
	}
	return val;
}

inline void DoPush(Value value)
{
	if(StackSize >= MAX_STACK) 
	{
		fprintf(stderr, "Stack Overflow at PC: %i! (Stack size: %i)", ProgramCounter, StackSize);
		exit(1);
	}

	Stack[StackSize++] = value;
}

inline Value DoPop()
{
	if(StackSize <= 0) 
	{
		fprintf(stderr, "Stack Underflow at PC: %i (Inst %i)!", ProgramCounter, Program[ProgramCounter]);
		exit(1);
	}

	return Stack[--StackSize];
}

void DoRead()
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
	
	Object* obj = NewObject(OBJ_STRING);
	obj->string = buffer;

	Value val;

	val.type = VAL_OBJ;
	val.obj = obj;

	DoPush(val);
}

void DoPushIndir(int nargs)
{
	IndirStack[IndirStackSize++] = nargs;
	IndirStack[IndirStackSize++] = FramePointer;
	IndirStack[IndirStackSize++] = ProgramCounter;

	FramePointer = StackSize;
}

void DoPopIndir()
{
	StackSize = FramePointer;

	int prevPc = IndirStack[--IndirStackSize];
	int prevFp = IndirStack[--IndirStackSize];
	int nargs = IndirStack[--IndirStackSize];
	
	StackSize -= nargs;
	FramePointer = prevFp;
	ProgramCounter = prevPc;
}

void ExecuteCycle()
{
	switch(Program[ProgramCounter])
	{
		case OP_PUSH:
		{
			++ProgramCounter;
			int cidx = ReadInteger();
			ConstInfo cip = Constants[cidx];

			if (cip.type == CST_NUM)
				DoPush(NewNumber(cip.number));
			else if (cip.type == CST_STR)
				DoPush(NewString(estrdup(cip.string)));
		} break;
		
		case OP_POP:
		{
			DoPop();
			++ProgramCounter;
		} break;
		
		#define BIN_OP(OP, operator) case OP_##OP: { Value val2 = DoPop(); Value val1 = DoPop(); DoPush(NewNumber(val1.number operator val2.number)); ++ProgramCounter; } break;
		#define BIN_OP_INT(OP, operator) case OP_##OP: { Value val2 = DoPop(); Value val1 = DoPop(); DoPush(NewNumber((int)val1.number operator (int)val2.number)); ++ProgramCounter; } break;
		
		case OP_MUL:
		{
			Value val2 = DoPop();
			Value val1 = DoPop();

			DoPush(NewNumber(val1.number * val2.number));

			++ProgramCounter;
		} break;

		BIN_OP(ADD, +)
		BIN_OP(SUB, -)
		BIN_OP(DIV, /)
		BIN_OP_INT(MOD, %)
		BIN_OP_INT(OR, |)
		BIN_OP_INT(AND, &)
		BIN_OP(LT, <)
		BIN_OP(LTE, <=)
		BIN_OP(GT, >)
		BIN_OP(GTE, >=)

		#undef BIN_OP
		
		case OP_EQU:
		{
			++ProgramCounter;
			Value b = DoPop();
			Value a = DoPop();

			if (a.type != b.type)
				DoPush(NewNumber(0));
			else
			{
				if (a.type == VAL_NUM)
					DoPush(NewNumber(a.number == b.number));
				else
				{
					if (a.obj->type != b.obj->type)
						DoPush(NewNumber(0));
					else if(a.obj->type == OBJ_STRING)
						DoPush(NewNumber(strcmp(a.obj->string, b.obj->string) == 0));
					else if (a.obj->type == OBJ_NATIVE)
						DoPush(NewNumber(a.obj->nat.addr == b.obj->nat.addr));
				}
			}
		} break;

		case OP_LOG_NOT:
		{
			++ProgramCounter;
			Value a = DoPop();

			if (a.type == VAL_NUM)
				DoPush(NewNumber(a.number > 0 ? -1 : 1));
			else
				DoPush(NewNumber(-1));	// Anything other than a negative number is always true
		} break;

		case OP_LOG_AND:
		{
			++ProgramCounter;
			Value b = DoPop();
			Value a = DoPop();

			DoPush(NewNumber((int)a.number && (int)b.number));
		} break;

		case OP_LOG_OR:
		{
			++ProgramCounter;
			Value b = DoPop();
			Value a = DoPop();

			DoPush(NewNumber((int)a.number || (int)b.number));
		} break;

		case OP_PRINT:
		{
			Value val = DoPop();
			if(val.type == VAL_NUM) printf("%g\n", val.number);
			else if (val.obj->type == OBJ_STRING) printf("%s\n", val.obj->string);
			else if (val.obj->type == OBJ_NATIVE) printf("<native at %p>\n", val.obj->nat.addr);
			++ProgramCounter;
		} break;

		case OP_SET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			GlobalVars[varIdx] = DoPop();
		} break;
		
		case OP_GET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			DoPush(GlobalVars[varIdx]); 
		} break;
		
		case OP_READ:
		{
			DoRead();
			++ProgramCounter;
		} break;
		
		case OP_GOTO:
		{
			++ProgramCounter;
			int newPc = ReadInteger();
			ProgramCounter = newPc;
		} break;
		
		case OP_GOTOZ:
		{
			++ProgramCounter;
			int newPc = ReadInteger();
			
			Value val = DoPop();

			// Only negative numbers result in jumps
			if(val.type == VAL_NUM && val.number <= 0)
				ProgramCounter = newPc;
		} break;
		
		case OP_CALL:
		{
			++ProgramCounter;
			int nargs = ReadInteger();
			int pcIdx = ReadInteger();
			
			DoPushIndir(nargs);
			ProgramCounter = FunctionPcs[pcIdx];
		} break;
		
		case OP_RETURN:
		{
			RetVal.type = VAL_NUM;
			RetVal.number = -1;

			DoPopIndir();
		} break;
		
		case OP_RETURN_VALUE:
		{
			RetVal = DoPop();
			DoPopIndir();
		} break;
		
		case OP_CALLF:
		{
			++ProgramCounter;
			
			int nargs = ReadInteger();
			int fIdx = ReadInteger();

			// the state of the stack prior to the function arguments being pushed
			int prevSize = StackSize - nargs;

			RetVal = ForeignFunctions[fIdx](&Stack[prevSize], nargs);
			
			// Resize the stack so that it has the arguments removed
			StackSize = prevSize;
		} break;

		case OP_GETLOCAL:
		{
			++ProgramCounter;
			int localIdx = ReadInteger();
			DoPush(Stack[FramePointer + localIdx]);
		} break;
		
		case OP_SETLOCAL:
		{
			++ProgramCounter;
			int localIdx = ReadInteger();
			Value val = DoPop();
			Stack[FramePointer + localIdx] = val;
		} break;

		case OP_GET_RETVAL:
		{
			++ProgramCounter;
			DoPush(RetVal);
		} break;

		case OP_HALT:
		{
			ProgramCounter = -1;
		} break;
	}

	// Only collect garbage in between iterations
	if (NumObjects >= MaxNumObjects)
		GarbageCollect();
}

void RunMachine()
{
	ProgramCounter = 0;
	while(ProgramCounter < ProgramLength && ProgramCounter >= 0)
		ExecuteCycle();
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
	TOK_OR = -30
};

#define MAX_TOK_LEN		256
char TokenBuffer[MAX_TOK_LEN];
double TokenNumber;

int Peek(FILE* in)
{
	int c = getc(in);
	ungetc(c, in);
	return c;
}

int GetToken(FILE* in)
{
	static int last = ' ';
	while (isspace(last))
	{
		if (last == '\n')
			++LineNumber;

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

		if(strcmp(TokenBuffer, "true") == 0)
		{
			TokenNumber = 1;
			return TOK_NUMBER;
		}
		
		if(strcmp(TokenBuffer, "false") == 0)
		{
			TokenNumber = 0;
			return TOK_NUMBER;
		}
		
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
		return GetToken(in);
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

#define MAX_READ_WRITE	16

typedef struct sExpr
{
	ExprType type;
	struct sExpr* next;

	union
	{
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

int GetNextToken(FILE* in)
{
	CurTok = GetToken(in);
	return CurTok;
}

Expr* ParseExpr(FILE* in);

static void ExpectToken(int tok, const char* msg)
{
	if (CurTok != tok)
	{
		fprintf(stderr, "%s(%i):", FileName, LineNumber);
		fprintf(stderr, msg);
		putc('\n', stderr);

		exit(1);
	}
}

static Expr* ParseIf(FILE* in)
{
	Expr* exp = Expr_create(EXP_IF);

	GetNextToken(in);

	exp->ifx.cond = ParseExpr(in);
	exp->ifx.body = ParseExpr(in);

	if (CurTok == TOK_ELSE)
	{
		GetNextToken(in);
		exp->ifx.alt = ParseExpr(in);
	}
	else
		exp->ifx.alt = NULL;

	return exp;
}

Expr* ParseFactor(FILE* in)
{
	switch(CurTok)
	{
		case '{':
		{
			GetNextToken(in);

			OpenScope();

			Expr* curExp = ParseExpr(in);
			Expr* head = curExp;

			while (CurTok != '}')
			{
				curExp->next = ParseExpr(in);
				curExp = curExp->next;
			}
			GetNextToken(in);

			CloseScope();

			Expr* exp = Expr_create(EXP_BLOCK);

			exp->block.exprHead = head;

			return exp;
		} break;

		case TOK_IDENT:
		{
			char* ident = estrdup(TokenBuffer);
			GetNextToken(in);
			if(CurTok != '(')
			{
				Expr* exp;
				
				exp = Expr_create(EXP_ID);
				
				exp->id.sym = ReferenceVariable(ident);
				exp->id.name = ident;

				return exp;
			}
			
			Expr* exp = Expr_create(EXP_CALL);
			
			GetNextToken(in);
			exp->call.numArgs = 0;
			
			while(CurTok != ')')
			{
				if (exp->call.numArgs >= MAX_ARGS)
				{
					fprintf(stderr, "Exceeded maximum number of arguments in call expression (%d).\n", MAX_ARGS);
					exit(1);
				}

				exp->call.args[exp->call.numArgs++] = ParseExpr(in);

				if(CurTok == ',') GetNextToken(in);
				else if(CurTok != ')')
				{
					fprintf(stderr, "Expected ')' after attempted call to proc %s\n", ident);
					exit(1);
				}
			}

			exp->call.calleeName = ident;

			GetNextToken(in);
			return exp;
		} break;
		
		case '-': case '+': case TOK_NOT:
		{
			int op = CurTok;
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_UNARY);
			exp->unary.op = op;
			exp->unary.exp = ParseFactor(in);
			return exp;
		} break;
		
		case TOK_NUMBER:
		{
			Expr* exp = Expr_create(EXP_NUM);
			exp->number = RegisterNumber(TokenNumber);
			GetNextToken(in);
			return exp;
		} break;

		case TOK_STRING:
		{
			Expr* exp = Expr_create(EXP_STRING);
			exp->string = RegisterString(TokenBuffer);
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_PROC:
		{
			if(CurrFunc)
			{
				fprintf(stderr, "Attempted to define function inside function '%s'. This is not allowed.\n", CurrFunc->name);
				exit(1);
			}
			
			Expr* exp = Expr_create(EXP_PROC);
			
			GetNextToken(in);

			ExpectToken(TOK_IDENT, "Function name must be identifier!");
			
			exp->proc.decl = DeclareFunction(TokenBuffer);
			CurrFunc = exp->proc.decl;

			GetNextToken(in);
			
			ExpectToken('(', "Expected '(' after function name");
			GetNextToken(in);
			
			while(CurTok != ')')
			{
				ExpectToken(TOK_IDENT, "Expected identifier in function parameter list");

				DeclareArgument(TokenBuffer);
				GetNextToken(in);
				
				if (CurTok != ')' && CurTok != ',')
				{
					fprintf(stderr, "Expected ')' or ',' after parameter name in function parameter list");
					exit(1);
				}

				if(CurTok == ',') GetNextToken(in);
			}
			
			GetNextToken(in);

			OpenScope();
			
			exp->proc.body = ParseExpr(in);

			CloseScope();

			CurrFunc = NULL;

			return exp;
		} break;
		
		case TOK_IF:
		{
			return ParseIf(in);
		} break;
		
		case TOK_WHILE:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_WHILE);

			exp->whilex.cond = ParseExpr(in);

			OpenScope();
			
			exp->whilex.body = ParseExpr(in);
			
			CloseScope();


			return exp;
		} break;

		case TOK_FOR:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_FOR);
			
			// Every local declared after this is scoped to the for
			OpenScope();

			exp->forx.init = ParseExpr(in);

			ExpectToken(';', "Expected ';' after for initializer.");

			GetNextToken(in);

			exp->forx.cond = ParseExpr(in);

			ExpectToken(';', "Expected ';' after for condition.");

			GetNextToken(in);

			exp->forx.step = ParseExpr(in);

			exp->forx.body = ParseExpr(in);

			CloseScope();

			return exp;
		} break;
		
		case TOK_RETURN:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_RETURN);
			if(CurTok == ';')
			{
				GetNextToken(in);	
				exp->retExpr = NULL;
				return exp;
			}

			exp->retExpr = ParseExpr(in);
			return exp;
		} break;

		case '(':
		{
			GetNextToken(in);
			Expr* inner = ParseExpr(in);
			assert(CurTok == ')' && "Expected matching ')' after previous '('");
			GetNextToken(in);
			
			Expr* exp = Expr_create(EXP_PAREN);
			exp->paren = inner;
			return exp;
		} break;
		
		default: break;
	}

	fprintf(stderr, "%s(%i): Unexpected token %i (%c)\n", FileName, LineNumber, CurTok, CurTok);
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

Expr* ParseBinRhs(FILE* in, int exprPrec, Expr* lhs)
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
			if (CurrFunc)
				lhs->id.sym = DeclareLocal(lhs->id.name);
			else
				lhs->id.sym = DeclareGlobalVar(lhs->id.name);
		}

		GetNextToken(in);

		Expr* rhs = ParseFactor(in);
		int nextPrec = GetTokenPrec();
		
		if(prec < nextPrec)
			rhs = ParseBinRhs(in, prec + 1, rhs);

		if (binOp == TOK_DECLARECONST)
		{
			if (lhs->type != EXP_ID)
			{
				fprintf(stderr, "Expected identifier to the left-hand side of '::'.\n");
				exit(1);
			}

			if (rhs->type == EXP_NUM)
				DeclareConst(lhs->id.name, rhs->number);
			else if (rhs->type == EXP_STRING)
				DeclareConst(lhs->id.name, rhs->string);
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

Expr* ParseExpr(FILE* in)
{
	Expr* factor = ParseFactor(in);
	return ParseBinRhs(in, 0, factor);
}

Expr* ParseProgram(FILE* in)
{
	GetNextToken(in);
		
	if(CurTok != TOK_EOF)
	{
		Expr* head = ParseExpr(in);
		Expr* exp = head;
		
		while(CurTok != TOK_EOF)
		{
			Expr* stmt = ParseExpr(in);
			head->next = stmt;
			head = stmt;
		}
		return exp;
	}
	return NULL;
}

void PrintProgram(Expr* program);

void PrintExpr(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_BLOCK:
		{
			Expr* node = exp->block.exprHead;
			printf("{\n");

			while (node)
			{
				PrintExpr(exp);
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
				PrintExpr(exp->call.args[i]);
				if(i + 1 < exp->call.numArgs) printf(",");
			}
			printf(")");
		} break;
		
		case EXP_NUM:
		{
			printf("%g", Constants[exp->number].number);
		} break;

		case EXP_STRING:
		{
			printf("%s", Constants[exp->string].string);
		} break;	
		
		case EXP_BINARY:
		{
			printf("(");
			PrintExpr(exp->binary.lhs);
			printf(" %c ", exp->binary.op);
			PrintExpr(exp->binary.rhs);
			printf(")");
		} break;
		
		case EXP_PAREN:
		{
			printf("(");
			PrintExpr(exp->paren);
			printf(")");
		} break;
		
		case EXP_UNARY:
		{
			printf("%c", exp->unary.op);
			PrintExpr(exp->unary.exp);
		} break;
		
		case EXP_PROC:
		{
			printf("func %s\n", exp->proc.decl->name);
			if (exp->proc.body)
				PrintExpr(exp->proc.body);

			printf("end\n");
		} break;
		
		case EXP_IF:
		{
			printf("if ");
			PrintExpr(exp->ifx.cond);
			if (exp->ifx.body)
				PrintExpr(exp->ifx.body);

			if (exp->ifx.alt)
			{
				printf("else\n");
				PrintProgram(exp->ifx.alt);
			}

			printf("end\n");
		} break;
		
		case EXP_WHILE:
		{
			printf("while ");
			PrintExpr(exp->whilex.cond);
			if (exp->whilex.body)
				PrintExpr(exp->ifx.body);
			printf("end\n");
		} break;
		
		case EXP_RETURN:
		{
			printf("return ");
			if(exp->retExpr)
				PrintExpr(exp->retExpr);
		} break;
		
		default:
		{
			printf("cannot print expression type %i\n", exp->type);
		} break;
	}
}

void PrintProgram(Expr* program)
{
	Expr* exp = program;
	printf("begin\n");
	while(exp)
	{
		PrintExpr(exp);
		exp = exp->next;
	}
	printf("\nend\n");
}

void CompileProgram(Expr* program);

static void CompileGetId(Expr* exp)
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
			GenerateCode(OP_GET);
		else if (exp->id.sym->type == SYM_LOCAL)
			GenerateCode(OP_GETLOCAL);

		GenerateInt(exp->id.sym->var.index);
	}
	else
	{
		GenerateCode(OP_PUSH);
		GenerateInt(exp->id.sym->constant.index);
	}
}

static void CompileExpr(Expr* exp);

static void CompileCall(Expr* exp)
{
	assert(exp->type == EXP_CALL);

	for (int i = 0; i < exp->call.numArgs; ++i)
		CompileExpr(exp->call.args[i]);

	Symbol* sym = ReferenceFunction(exp->call.calleeName);
	if (!sym)
	{
		fprintf(stderr, "Attempted to call undefined function '%s'.\n", exp->call.calleeName);
		exit(1);
	}

	if (sym->type == SYM_FOREIGN_FUNCTION)
	{
		GenerateCode(OP_CALLF);
		GenerateInt(exp->call.numArgs);
		GenerateInt(sym->foreignFunc.index);
	}
	else
	{
		GenerateCode(OP_CALL);
		GenerateInt(exp->call.numArgs);
		GenerateInt(sym->func.index);
	}
}

static void CompileExpr(Expr* exp)
{
	switch (exp->type)
	{
		case EXP_ID:
		{
			CompileGetId(exp);
		} break;

		case EXP_NUM:
		{
			GenerateCode(OP_PUSH);
			GenerateInt(exp->number);
		} break;

		case EXP_STRING:
		{
			GenerateCode(OP_PUSH);
			GenerateInt(exp->string);
		} break;

		case EXP_CALL:
		{
			CompileCall(exp);
			GenerateCode(OP_GET_RETVAL);
		} break;

		case EXP_BINARY:
		{
			switch (exp->binary.op)
			{
				case '+':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_ADD);
				} break;

				case '*':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_MUL);
				} break;

				case '/':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_DIV);
				} break;

				case '%':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_MOD);
				} break;

				case '|':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_OR);
				} break;

				case '&':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_AND);
				} break;

				case '-':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_SUB);
				} break;

				case '<':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_LT);
				} break;

				case '>':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_GT);
				} break;


				case TOK_EQUALS:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_EQU);
				} break;

				case TOK_NOTEQUALS:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_EQU);
					GenerateCode(OP_LOG_NOT);
				} break;

				case TOK_LTE:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_LTE);
				} break;

				case TOK_GTE:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_GTE);
				} break;

				case TOK_AND:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_LOG_AND);
				} break;

				case TOK_OR:
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_LOG_OR);
				} break;

				default:
					fprintf(stderr, "Found assignment when expecting expression.\n");
					exit(1);
					break;
			}
		} break;

		case EXP_PAREN:
		{
			CompileExpr(exp->paren);
		} break;

		case EXP_UNARY:
		{
			CompileExpr(exp->unary.exp);
			switch (exp->unary.op)
			{
				case '-':
				{
					GenerateCode(OP_PUSH);
					GenerateInt(RegisterNumber(-1));
					GenerateCode(OP_MUL);
				} break;

				case TOK_NOT:
				{
					GenerateCode(OP_LOG_NOT);
				} break;

				default:
					fprintf(stderr, "Unsupported unary operator %c (%d)\n", exp->unary.op, exp->unary.op);
					exit(1);
					break;
			}
		} break;
	}
}

static void CompileStatement(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_CALL:
		{
			CompileCall(exp);
		} break;

		case EXP_BLOCK:
		{
			Expr* node = exp->block.exprHead;

			while (node)
			{
				CompileStatement(node);
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
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_ADD);
							} break;

							case TOK_MINUSEQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_SUB);
							} break;

							case TOK_MULEQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_MUL);
							} break;

							case TOK_DIVEQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_DIV);
							} break;

							case TOK_MODEQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_MOD);
							} break;

							case TOK_ANDEQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_AND);
							} break;

							case TOK_OREQUAL:
							{
								CompileGetId(exp->binary.lhs);
								CompileExpr(exp->binary.rhs);
								GenerateCode(OP_OR);
							} break;

							default:
								CompileExpr(exp->binary.rhs);
								break;
						}

						if (!exp->binary.lhs->id.sym)
						{
							// The variable being referenced doesn't exist
							fprintf(stderr, "Assigning to undeclared identifier '%s'.\n", exp->binary.lhs->id.name);
							exit(1);
						}

						if (exp->binary.lhs->id.sym->type == SYM_GLOBAL)
							GenerateCode(OP_SET);
						else if (exp->binary.lhs->id.sym->type == SYM_LOCAL)
							GenerateCode(OP_SETLOCAL);
						else		// Probably a constant, can't change it
						{
							fprintf(stderr, "Cannot assign to id '%s'.\n", exp->binary.lhs->id.name);
							exit(1);
						}

						GenerateInt(exp->binary.lhs->id.sym->var.index);
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
			GenerateCode(OP_GOTO);
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			
			FunctionPcs[exp->proc.decl->func.index] = ProgramLength;
			
			for(int i = 0; i < exp->proc.decl->func.nlocals; ++i)
			{
				GenerateCode(OP_PUSH);
				GenerateInt(RegisterNumber(0));
			}
			
			if (exp->proc.body)
				CompileStatement(exp->proc.body);

			GenerateCode(OP_RETURN);
			GenerateIntAt(ProgramLength, skipGotoPc);
		} break;
		
		case EXP_IF:
		{
			CompileExpr(exp->ifx.cond);
			GenerateCode(OP_GOTOZ);
			
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			
			if(exp->ifx.body)
				CompileStatement(exp->ifx.body);
			
			GenerateCode(OP_GOTO);
			int exitGotoPc = ProgramLength;
			GenerateInt(0);

			GenerateIntAt(ProgramLength, skipGotoPc);

			if (exp->ifx.alt)
				CompileStatement(exp->ifx.alt);

			GenerateIntAt(ProgramLength, exitGotoPc);
		} break;
		
		case EXP_WHILE:
		{
			int condPc = ProgramLength;
			
			CompileExpr(exp->whilex.cond);
			
			GenerateCode(OP_GOTOZ);
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			
			if(exp->whilex.body)
				CompileStatement(exp->whilex.body);
			
			GenerateCode(OP_GOTO);
			GenerateInt(condPc);

			GenerateIntAt(ProgramLength, skipGotoPc);
		} break;
		
		case EXP_FOR:
		{
			CompileStatement(exp->forx.init);

			int condPc = ProgramLength;
			CompileExpr(exp->forx.cond);

			GenerateCode(OP_GOTOZ);
			int skipGotoPc = ProgramLength;
			GenerateInt(0);

			if (exp->forx.body)
				CompileStatement(exp->forx.body);

			CompileStatement(exp->forx.step);
			
			GenerateCode(OP_GOTO);
			GenerateInt(condPc);

			GenerateIntAt(ProgramLength, skipGotoPc);
		} break;

		case EXP_RETURN:
		{
			if(exp->retExpr)
			{
				CompileExpr(exp->retExpr);
				GenerateCode(OP_RETURN_VALUE);
			}
			else
				GenerateCode(OP_RETURN);
		} break;
	}
}

void CompileProgram(Expr* program)
{
	Expr* exp = program;
	while(exp)
	{
		CompileStatement(exp);
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

		case EXP_NUM: case EXP_STRING: break;
		
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

void DebugMachineProgram()
{
	for(int i = 0; i < ProgramLength; ++i)
	{
		switch(Program[i])
		{
			case OP_PUSH:			printf("push\n"); break;
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

static void CheckInitialized()
{
	Symbol* node = GlobalSymbols;

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
static void BuildForeignFunctions(void)
{
	Symbol* node = GlobalSymbols;

	while (node)
	{
		if (node->type == SYM_FOREIGN_FUNCTION)
			ForeignFunctions[node->foreignFunc.index] = node->foreignFunc.callee;

		node = node->next;
	}
}

void CompileFile(FILE* in)
{
	CurTok = 0;
	Expr* prog = ParseProgram(in);

	// Allocate room for vm execution info
	GlobalVars = calloc(NumGlobalVars, sizeof(Value));
	FunctionPcs = calloc(NumFunctions, sizeof(int));
	ForeignFunctions = calloc(NumForeignFunctions, sizeof(ForeignFunction));

	assert(GlobalVars &&
		FunctionPcs &&
		ForeignFunctions);

	BuildForeignFunctions();

	CompileProgram(prog);
	GenerateCode(OP_HALT);

	CheckInitialized();		// Done after compilation because it might have registered undefined functions during the compilation stage
	
	DeleteProgram(prog);
}
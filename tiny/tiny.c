// tiny -- an bytecode-based interpreter for the tiny language
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

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
		if (obj->ptrFree)
			obj->ptrFree(obj->ptr);
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
		if(obj->ptrMark)
			obj->ptrMark(obj->ptr);
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

Object* NewObject(ObjectType type, char gc)
{
	if(gc && NumObjects >= MaxNumObjects) GarbageCollect();
	
	Object* obj = emalloc(sizeof(Object));
	
	obj->type = type;
	obj->next = GCHead;
	GCHead = obj;
	obj->marked = 0;
	
	NumObjects++;
	
	return obj;
}

Object* NewNative(void* ptr, void(*ptrFree)(void*), void(*ptrMark)(void*))
{
	Object* obj = NewObject(OBJ_NATIVE, 1);
	obj->ptr = ptr;
	obj->ptrFree = ptrFree;
	obj->ptrMark = ptrMark;
	return obj;
}

Object* NewNumber(double value)
{
	Object* obj = NewObject(OBJ_NUM, 1);
	obj->number = value;
	return obj;
}

Object* NewString(char* string)
{
	Object* obj = NewObject(OBJ_STRING, 1);
	obj->string = string;
	return obj;
}

Object* Stack[MAX_STACK];
int StackSize = 0;

int IndirStack[MAX_INDIR];
int IndirStackSize;

struct 
{
	char initialized;
	char* name;
	Object* object;
} Variables[MAX_VARS];
int VariableAmount = 0;

char* FunctionNames[MAX_FUNCS];
int FunctionPcs[MAX_FUNCS];
int FunctionAmount = 0;

char* ForeignFunctionNames[MAX_FUNCS];
void (*ForeignFunctions[MAX_FUNCS])(void);
int ForeignAmount = 0;

int CurrScope = 0;
int CurrNumLocals = 0;

int NumArgsDeclared = 0;

typedef struct sLocalDecl
{
	struct sLocalDecl* next;
	char* name;
	int index;
	int scope;
} LocalDecl;

LocalDecl* LocalDeclarations;

int DeclareLocal(char* name)
{
	LocalDecl* newLocal = emalloc(sizeof(LocalDecl));
	
	newLocal->name = estrdup(name);
	newLocal->index = CurrNumLocals;
	newLocal->scope = CurrScope;
	
	newLocal->next = LocalDeclarations;
	LocalDeclarations = newLocal;
	
	++CurrNumLocals;
	return CurrNumLocals - 1;
}

int DeclareArgument(char* name, int nargs)
{
	++NumArgsDeclared;
	
	LocalDecl* newLocal = emalloc(sizeof(LocalDecl));
	
	newLocal->name = estrdup(name);
	newLocal->index = -nargs + (NumArgsDeclared - 1);
	newLocal->scope = CurrScope;
	
	newLocal->next = LocalDeclarations;
	LocalDeclarations = newLocal;
	
	return newLocal->index;
}

int ReferenceLocal(char* name)
{
	LocalDecl* node = LocalDeclarations;
	
	while(node)
	{
		if(strcmp(node->name, name) == 0)
		{
			if(node->scope <= CurrScope)
				return node->index;
		}
		node = node->next;
	}
	
	fprintf(stderr, "Attempted to reference non-existent local variable '%s'\n", name);
	exit(1);
}

void ClearLocals()
{
	LocalDecl* node = LocalDeclarations;
	
	while(node)
	{
		LocalDecl* next = node->next;
		free(node->name);
		free(node);
		node = next;
	}
	
	LocalDeclarations = NULL;
	
	CurrNumLocals = 0;
}

typedef enum
{
	CST_NUM,
	CST_STR
} ConstType;

typedef struct
{
	ConstType type;
	union
	{
		char* string;
		double number;
	};
} ConstInfo;

ConstInfo** Constants;
int ConstantCapacity = 1;
int ConstantAmount = 0;

ConstInfo* NewConst(ConstType type)
{
	if(ConstantAmount + 1 >= ConstantCapacity)
	{
		ConstantCapacity *= 2;
		Constants = erealloc(Constants, ConstantCapacity * sizeof(ConstInfo));
	}
	
	ConstInfo* info = emalloc(sizeof(ConstInfo));
	info->type = type;
	return info;
}

void MarkAll()
{
	for(int i = 0; i < StackSize; ++i)
		Mark(Stack[i]);
	for (int i = 0; i < MAX_VARS; ++i)
	{
		if(Variables[i].object)
			Mark(Variables[i].object);
	}
}

void DeleteVariables()
{
	for(int i = 0; i < VariableAmount; ++i)
		free(Variables[i].name);
	VariableAmount = 0;
}

void DeleteFunctions()
{
	for(int i = 0; i < FunctionAmount; ++i)
		free(FunctionNames[i]);
	FunctionAmount = 0;
}

void ResetMachine()
{
	ProgramLength = 0;
	ProgramCounter = -1;
	ConstantAmount = 0;
	StackSize = 0;
	FramePointer = 0;
	VariableAmount = 0;
	FunctionAmount = 0;
	IndirStackSize = 0;
	GCHead = NULL;
	NumObjects = 0;
	ForeignAmount = 0;
	CurrScope = 0;
	LocalDeclarations = NULL;
	NumArgsDeclared = 0;
	memset(Variables, 0, sizeof(Variables));
}

void GenerateCode(Word inst)
{	
	assert(ProgramLength < MAX_PROG_LEN && "Program Overflow!");
	Program[ProgramLength++] = inst;
}

void GenerateInt(int value)
{
	Word* wp = (Word*)(&value);
	for(int i = 0; i < 4; ++i)
		GenerateCode(*wp++);
}

void GenerateIntAt(int value, int pc)
{
	Word* wp = (Word*)(&value);
	for(int i = 0; i < 4; ++i)
		Program[pc + i] = *wp++;
}

int RegisterNumber(double value)
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		if(Constants[i]->type == CST_NUM)
		{
			if(Constants[i]->number == value)
				return i;
		}
	}
	
	assert(ConstantAmount < MAX_CONST_AMT && "Constant Overflow!");
	ConstInfo* num = NewConst(CST_NUM);
	num->number = value;
	Constants[ConstantAmount++] = num;
	return ConstantAmount - 1;
}

int RegisterString(char* string)
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		if(Constants[i]->type == CST_STR)
		{
			if(strcmp(Constants[i]->string, string) == 0)
				return i;
		}
	}
	
	assert(ConstantAmount < MAX_CONST_AMT && "Constant Overflow!");
	
	ConstInfo* str = NewConst(CST_STR);
	str->string = estrdup(string);
	Constants[ConstantAmount++] = str;
	return ConstantAmount - 1;
}

void DeleteConstants()
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		ConstInfo* cip = Constants[i];
		if(cip->type == CST_STR)
			free(cip->string);
		free(cip);
	}
	free(Constants);
}

int RegisterVariableName(const char* name)
{
	for(int i = 0; i < VariableAmount; ++i)
	{
		if(strcmp(Variables[i].name, name) == 0)
			return i;
	}
	
	Variables[VariableAmount++].name = estrdup(name);
	return VariableAmount - 1;
}

static int RegisterFunction(const char* name)
{
	for(int i = 0; i < ForeignAmount; ++i)
	{
		if(strcmp(ForeignFunctionNames[i], name) == 0)
			return (-i - 1);
	}

	for(int i = 0; i < FunctionAmount; ++i)
	{
		if(strcmp(FunctionNames[i], name) == 0)
			return i;
	}

	FunctionNames[FunctionAmount++] = estrdup(name);
	return FunctionAmount - 1;
}

int GetProcId(const char* name)
{
	for(int i = 0; i < FunctionAmount; ++i)
	{
		if(strcmp(FunctionNames[i], name) == 0)
			return i;
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

void BindForeignFunction(void(*fun)(void), char* name)
{
	ForeignFunctionNames[ForeignAmount] = estrdup(name);
	ForeignFunctions[ForeignAmount++] = fun;
}

void DeleteForeignFunctions()
{
	for(int i = 0; i < ForeignAmount; ++i)
		free(ForeignFunctionNames[i]);
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
	OP_NEQU,
	
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

void DoPush(Object* value)
{
	assert(value);
	if(StackSize >= MAX_STACK) 
	{
		fprintf(stderr, "Stack Overflow at PC: %i! (Stack size: %i)", ProgramCounter, StackSize);
		exit(1);
	}
	Stack[StackSize++] = value;
}

Object* DoPop()
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
	
	Object* obj = NewObject(OBJ_STRING, 1);
	obj->string = buffer;
	DoPush(obj);
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
			ConstInfo* cip = Constants[cidx];
			Object* obj = NULL;
			if (cip->type == CST_NUM)
			{
				obj = NewObject(OBJ_NUM, 1);
				obj->number = cip->number;
			}
			else if (cip->type == CST_STR)
			{
				obj = NewObject(OBJ_STRING, 1);
				obj->string = estrdup(cip->string);
			}

			DoPush(obj);
		} break;
		
		case OP_POP:
		{
			DoPop();
			++ProgramCounter;
		} break;
		
		#define BIN_OP(OP, operator) case OP_##OP: { Object* val2 = DoPop(); Object* val1 = DoPop(); Object* result = NewObject(OBJ_NUM, 0); result->number = val1->number operator val2->number; DoPush(result); ++ProgramCounter; } break;
		#define BIN_OP_INT(OP, operator) case OP_##OP: { Object* val2 = DoPop(); Object* val1 = DoPop(); Object* result = NewObject(OBJ_NUM, 0); result->number = (int)val1->number operator (int)val2->number; DoPush(result); ++ProgramCounter; } break;
		
		case OP_MUL:
		{
			Object* val2 = DoPop();
			Object* val1 = DoPop();

			Object* result = NewObject(OBJ_NUM, 0);
			result->number = val1->number * val2->number;

			DoPush(result);
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
		BIN_OP(EQU, ==)
		BIN_OP(NEQU, !=)
		
		#undef BIN_OP
		
		case OP_PRINT:
		{
			Object* obj = DoPop();
			if(obj->type == OBJ_NUM) printf("%g\n", obj->number);
			if(obj->type == OBJ_STRING) printf("%s\n", obj->string);
			++ProgramCounter;
		} break;

		case OP_SET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			Variables[varIdx].object = DoPop();
		} break;
		
		case OP_GET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			DoPush(Variables[varIdx].object); 
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
			if(DoPop()->number == 0)
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
			DoPopIndir();
		} break;
		
		case OP_RETURN_VALUE:
		{
			Object* retVal = DoPop();
			DoPopIndir();
			DoPush(retVal);
		} break;
		
		case OP_CALLF:
		{
			++ProgramCounter;
			int fIdx = ReadInteger();
			ForeignFunctions[fIdx]();
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
			Object* val = DoPop();
			Stack[FramePointer + localIdx] = val;
		} break;

		case OP_HALT:
		{
			ProgramCounter = -1;
		} break;
	}
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
	TOK_READ = -5,
	TOK_WRITE = -6,
	TOK_NUMBER = -7,
	TOK_STRING = -8,
	TOK_LOCAL = -9,
	TOK_PROC = -10,
	TOK_IF = -11,
	TOK_EQUALS = -12,
	TOK_NOTEQUALS = -13,
	TOK_LTE = -14,
	TOK_GTE = -15,
	TOK_RETURN = -16,
	TOK_WHILE = -17,
	TOK_FOR = -18,
	TOK_DO = -19,
	TOK_THEN = -20,
	TOK_ELSE = -21,
	TOK_EOF = -22,
	TOK_LOCALREF = -23,
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
	while(isspace(last))
		last = getc(in);
	
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
		if (strcmp(TokenBuffer, "read") == 0) return TOK_READ;
		if (strcmp(TokenBuffer, "write") == 0) return TOK_WRITE;
		if (strcmp(TokenBuffer, "func") == 0) return TOK_PROC;
		if (strcmp(TokenBuffer, "if") == 0) return TOK_IF;
		if (strcmp(TokenBuffer, "return") == 0) return TOK_RETURN;
		if (strcmp(TokenBuffer, "while") == 0) return TOK_WHILE;
		if (strcmp(TokenBuffer, "then") == 0) return TOK_THEN;
		if (strcmp(TokenBuffer, "local") == 0) return TOK_LOCAL;
		if (strcmp(TokenBuffer, "for") == 0) return TOK_FOR;
		if (strcmp(TokenBuffer, "do") == 0) return TOK_DO;
		if (strcmp(TokenBuffer, "else") == 0) return TOK_ELSE;

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
	
	if(last == '$')
	{
		last = getc(in);
		
		int i = 0;
		while(isalnum(last) || last == '_')
		{
			assert(i < MAX_TOK_LEN - 1 && "Token was too long!");
			TokenBuffer[i++] = last;
			last = getc(in);
		}
		TokenBuffer[i] = '\0';
		
		return TOK_LOCALREF;
	}

	if(last == '"')
	{
		last = getc(in);
		int i = 0;
		while(last != '"')
		{
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
		
	int lastChar = last;
	last = getc(in);
	return lastChar;
}

typedef enum
{
	EXP_READ,
	EXP_WRITE,
	EXP_ID,
	EXP_CALL,
	EXP_NUM,
	EXP_STRING,
	EXP_BINARY,
	EXP_PAREN,
	EXP_PROC,
	EXP_IF,
	EXP_UNARY,
	EXP_RETURN,
	EXP_WHILE,
	EXP_FOR,
	EXP_LOCAL,
	EXP_LOCALREF
} ExprType;

#define MAX_READ_WRITE	16

typedef struct sExpr
{
	ExprType type;
	struct sExpr* next;

	union
	{
		struct
		{
			struct sExpr* head;
		} program;
		
		struct
		{
			int ids[MAX_READ_WRITE];
			char isLocal[MAX_READ_WRITE];
			int numIds;
		} read;
		
		struct
		{
			struct sExpr* exprs[MAX_READ_WRITE];
			int numExprs;
		} write;
		
		int number;
		int string;

		int ident;
		
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
			int name;
			struct sExpr* exprHead;
			
			int numLocals;
		} proc;

		struct
		{
			struct sExpr* cond;
			struct sExpr* bodyHead;
			struct sExpr* alt;
		} ifx;
		
		struct
		{
			struct sExpr* cond;
			struct sExpr* exprHead;
		} whilex;

		struct
		{
			struct sExpr* init;
			struct sExpr* cond;
			struct sExpr* step;
			struct sExpr* exprHead;
		} forx;
		
		struct
		{
			int index;
		} local; // used for both EXP_LOCAL and EXP_LOCALREF
		
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
		fprintf(stderr, msg);
		putc('\n', stderr);

		exit(1);
	}
}

// Parses expressions until TOK_END is encountered
// Expressions are put in a linked list referenced by the first expression
static Expr* ParseExprList(FILE* in)
{
	if (CurTok == TOK_END)
	{
		GetNextToken(in);
		return NULL;
	}

	Expr* curExp = ParseExpr(in);
	Expr* head = curExp;

	while (CurTok != TOK_END)
	{
		curExp->next = ParseExpr(in);
		curExp = curExp->next;
	}
	GetNextToken(in);

	return head;
}

static Expr* ParseIf(FILE* in)
{
	Expr* exp = Expr_create(EXP_IF);

	GetNextToken(in);

	exp->ifx.cond = ParseExpr(in);

	ExpectToken(TOK_THEN, "Expected 'then' after if.");
	GetNextToken(in);

	Expr* curExp = ParseExpr(in);
	
	exp->ifx.bodyHead = curExp;

	while (CurTok != TOK_END && CurTok != TOK_ELSE)
	{
		curExp->next = ParseExpr(in);
		curExp = curExp->next;
	}

	if (CurTok == TOK_ELSE)
	{
		GetNextToken(in);

		exp->ifx.alt = ParseExpr(in);
	}
	else
	{
		exp->ifx.alt = NULL;
		GetNextToken(in);
	}

	return exp;
}

Expr* ParseFactor(FILE* in)
{
	switch(CurTok)
	{
		case TOK_IDENT:
		{
			char* ident = estrdup(TokenBuffer);
			GetNextToken(in);
			if(CurTok != '(')
			{
				Expr* exp;
				
				exp = Expr_create(EXP_ID);
				exp->ident = RegisterVariableName(ident);
				free(ident);
				return exp;
			}
			
			Expr* exp = Expr_create(EXP_CALL);
			
			GetNextToken(in);
			exp->call.numArgs = 0;
			
			while(CurTok != ')')
			{
				exp->call.args[exp->call.numArgs++] = ParseExpr(in);
				if(CurTok == ',') GetNextToken(in);
				else if(CurTok != ')')
				{
					fprintf(stderr, "Expected ')' after attempted call to proc %s\n", ident);
					exit(1);
				}
			}

			exp->call.calleeName = estrdup(ident);
			
			free(ident);

			GetNextToken(in);
			return exp;
		} break;
		
		case '-': case '+':
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
		
		case TOK_LOCAL:
		{
			if(CurrScope == 0) 
			{
				fprintf(stderr, "Cannot declare or reference locals in the global scope!\n");
				exit(1);
			}
			
			GetNextToken(in);
			ExpectToken(TOK_IDENT, "Local name must be identifier!");
			
			Expr* exp = Expr_create(EXP_LOCAL);
			exp->local.index = DeclareLocal(TokenBuffer);
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_LOCALREF:
		{
			int idx = ReferenceLocal(TokenBuffer);
			GetNextToken(in);
			
			Expr* exp = Expr_create(EXP_LOCALREF);
			exp->local.index = idx;
			return exp;
		} break;
		
		case TOK_PROC:
		{
			if(CurrScope != 0)
			{
				fprintf(stderr, "Procedure defintion in a local scope is not allowed\n");
				exit(1);
			}
			
			Expr* exp = Expr_create(EXP_PROC);
			
			GetNextToken(in);

			ExpectToken(TOK_IDENT, "Function name must be identifier!");
			
			exp->proc.name = RegisterFunction(TokenBuffer);
			
			GetNextToken(in);
			
			++CurrScope;
			
			ExpectToken('(', "Expected '(' after function name");
			GetNextToken(in);
			
			char* args[MAX_ARGS];
			int nargs = 0;
			
			while(CurTok != ')')
			{
				ExpectToken(TOK_IDENT, "Expected identifier in function parameter list");

				args[nargs++] = estrdup(TokenBuffer);
				GetNextToken(in);
				
				if (CurTok != ')' && CurTok != ',')
				{
					fprintf(stderr, "Expected ')' or ',' after parameter name in function parameter list");
					exit(1);
				}

				if(CurTok == ',') GetNextToken(in);
			}
			
			for(int i = 0; i < nargs; ++i)
				DeclareArgument(args[i], nargs);
			
			NumArgsDeclared = 0;
			
			for(int i = 0; i < nargs; ++i)
				free(args[i]);
			
			GetNextToken(in);
			
			exp->proc.exprHead = ParseExprList(in);
			exp->proc.numLocals = CurrNumLocals;
			
			--CurrScope;
			
			ClearLocals();

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

			++CurrScope;
			exp->whilex.exprHead = ParseExprList(in);
			--CurrScope;

			return exp;
		} break;

		case TOK_FOR:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_FOR);
			
			// Every local declared after this is scoped to the for
			++CurrScope;

			exp->forx.init = ParseExpr(in);

			ExpectToken(';', "Expected ';' after for initializer.");

			GetNextToken(in);

			exp->forx.cond = ParseExpr(in);

			ExpectToken(';', "Expected ';' after for condition.");

			GetNextToken(in);

			exp->forx.step = ParseExpr(in);

			ExpectToken(TOK_DO, "Expected 'do' after for step expression.");

			GetNextToken(in);

			exp->forx.exprHead = ParseExprList(in);

			--CurrScope;

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
		
		case TOK_READ:
		{
			GetNextToken(in);
		
			Expr* exp = Expr_create(EXP_READ);
			exp->read.numIds = 0;
			while(CurTok != TOK_END)
			{
				assert((CurTok == TOK_IDENT || CurTok == TOK_LOCALREF) && "Expected some sort of variable in list for read expression!");
				if(CurTok == TOK_IDENT)
				{
					exp->read.isLocal[exp->read.numIds] = 0;

					int id = RegisterVariableName(TokenBuffer);
					exp->read.ids[exp->read.numIds++] = id;

					Variables[id].initialized = 1;
				}
				else
				{
					exp->read.isLocal[exp->read.numIds] = 1;
					exp->read.ids[exp->read.numIds++] = ReferenceLocal(TokenBuffer);
				}
				GetNextToken(in);
			}
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_WRITE:
		{
			GetNextToken(in);
		
			Expr* exp = Expr_create(EXP_WRITE);
			exp->write.numExprs = 0;
			while(CurTok != TOK_END)
			{
				Expr* writeExp = ParseExpr(in);
				exp->write.exprs[exp->write.numExprs++] = writeExp;
			}
			GetNextToken(in);
			
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

	fprintf(stderr, "Unexpected token %i (%c)\n", CurTok, CurTok);
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
		
		case '=':						prec = 1; break;
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

		GetNextToken(in);

		Expr* rhs = ParseFactor(in);
		int nextPrec = GetTokenPrec();
		
		if(prec < nextPrec)
			rhs = ParseBinRhs(in, prec + 1, rhs);

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
		case EXP_ID:
		{	
			printf("%s", Variables[exp->ident].name);
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
			printf("%g", Constants[exp->number]->number);
		} break;

		case EXP_STRING:
		{
			printf("%s", Constants[exp->string]->string);
		} break;	
		
		case EXP_READ:
		{
			printf("read ");
			for(int i = 0; i < exp->read.numIds; ++i)
				printf("%s ", Variables[exp->read.ids[i]].name);
			printf("end\n");
		} break;
		
		case EXP_WRITE:
		{
			printf("write ");
			for(int i = 0; i < exp->write.numExprs; ++i)
			{
				PrintExpr(exp->write.exprs[i]);
				printf(" ");
			}
			printf("end\n");
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
			printf("proc %s\n", FunctionNames[exp->proc.name]);
			if(exp->proc.exprHead)
				PrintProgram(exp->proc.exprHead);
			printf("end\n");
		} break;
		
		case EXP_IF:
		{
			printf("if ");
			PrintExpr(exp->ifx.cond);
			if(exp->ifx.bodyHead)
				PrintProgram(exp->ifx.bodyHead);

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
			if(exp->whilex.exprHead)
				PrintProgram(exp->whilex.exprHead);
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

void CompileExpr(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID:
		{
			if(!Variables[exp->ident].initialized)
			{
				fprintf(stderr, "Attempted to use uninitialized variable '%s'\n", Variables[exp->ident].name);
				exit(1);
			}
			
			GenerateCode(OP_GET);
			GenerateInt(exp->ident);
		} break;
		
		case EXP_CALL:
		{
			for(int i = 0; i < exp->call.numArgs; ++i)
				CompileExpr(exp->call.args[i]);
			
			int callee = RegisterFunction(exp->call.calleeName);

			if(callee < 0)
			{
				GenerateCode(OP_CALLF);
				GenerateInt(-(callee + 1));
			}
			else
			{
				GenerateCode(OP_CALL);
				GenerateInt(exp->call.numArgs);
				GenerateInt(callee);
			}
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

		case EXP_LOCAL:
		{
			// local expressions do not create code
		} break;
		
		case EXP_LOCALREF:
		{
			GenerateCode(OP_GETLOCAL);
			GenerateInt(exp->local.index);
		} break;
		
		case EXP_READ:
		{
			for(int i = 0; i < exp->read.numIds; ++i)
			{	
				GenerateCode(OP_READ);
				if(!exp->read.isLocal[i])
					GenerateCode(OP_SET);
				else
					GenerateCode(OP_SETLOCAL);
				GenerateInt(exp->read.ids[i]);
			}
		} break;
		
		case EXP_WRITE:
		{
			for(int i = 0; i < exp->write.numExprs; ++i)
			{
				CompileExpr(exp->write.exprs[i]);
				GenerateCode(OP_PRINT);
			}
		} break;
		
		case EXP_BINARY:
		{	
			switch(exp->binary.op)
			{
				case '=':
				{
					if(exp->binary.lhs->type == EXP_ID)
					{
						CompileExpr(exp->binary.rhs);
						GenerateCode(OP_SET);
						GenerateInt(exp->binary.lhs->ident);
						Variables[exp->binary.lhs->ident].initialized = 1;
					}
					else if(exp->binary.lhs->type == EXP_LOCAL || exp->binary.lhs->type == EXP_LOCALREF)
					{
						CompileExpr(exp->binary.rhs);
						GenerateCode(OP_SETLOCAL);
						GenerateInt(exp->binary.lhs->local.index);
					}
					else
					{
						fprintf(stderr, "LHS of assignment operation must be a local or a global variable\n");
						exit(1);
					}
				} break;
				
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
					GenerateCode(OP_NEQU);
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
			}
		} break;
		
		case EXP_PAREN:
		{
			CompileExpr(exp->paren);
		} break;
		
		case EXP_UNARY:
		{
			CompileExpr(exp->unary.exp);
			switch(exp->unary.op)
			{
				case '-': 
				{
					GenerateCode(OP_PUSH);
					GenerateInt(RegisterNumber(-1));
					GenerateCode(OP_MUL);
				} break;
			}
		} break;
		
		case EXP_PROC:
		{
			GenerateCode(OP_GOTO);
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			
			FunctionPcs[exp->proc.name] = ProgramLength;
			
			for(int i = 0; i < exp->proc.numLocals; ++i)
			{
				GenerateCode(OP_PUSH);
				GenerateInt(RegisterNumber(0));
			}
			
			if(exp->proc.exprHead)
				CompileProgram(exp->proc.exprHead);
			GenerateCode(OP_RETURN);
			GenerateIntAt(ProgramLength, skipGotoPc);
		} break;
		
		case EXP_IF:
		{
			CompileExpr(exp->ifx.cond);
			GenerateCode(OP_GOTOZ);
			
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			
			if(exp->ifx.bodyHead)
				CompileProgram(exp->ifx.bodyHead);
			
			GenerateIntAt(ProgramLength, skipGotoPc);

			if (exp->ifx.alt)
				CompileExpr(exp->ifx.alt);
		} break;
		
		case EXP_WHILE:
		{
			int condPc = ProgramLength;
			CompileExpr(exp->whilex.cond);
			GenerateCode(OP_GOTOZ);
			int skipGotoPc = ProgramLength;
			GenerateInt(0);
			if(exp->whilex.exprHead)
				CompileProgram(exp->whilex.exprHead);
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
		CompileExpr(exp);
		exp = exp->next;
	}
}

void DeleteProgram(Expr* program);

void Expr_destroy(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID: case EXP_NUM: case EXP_STRING: case EXP_LOCAL: case EXP_LOCALREF: break;
		
		case EXP_CALL: 
		{
			free(exp->call.calleeName);
			for(int i = 0; i < exp->call.numArgs; ++i)
				Expr_destroy(exp->call.args[i]);
		} break;
		
		case EXP_READ: break;
		case EXP_WRITE: for(int i = 0; i < exp->write.numExprs; ++i) Expr_destroy(exp->write.exprs[i]); break;
		case EXP_BINARY: Expr_destroy(exp->binary.lhs); Expr_destroy(exp->binary.rhs); break;
		case EXP_PAREN: Expr_destroy(exp->paren); break;
		case EXP_PROC: if(exp->proc.exprHead) DeleteProgram(exp->proc.exprHead); break;
		case EXP_IF: Expr_destroy(exp->ifx.cond); if (exp->ifx.bodyHead) DeleteProgram(exp->ifx.bodyHead); if (exp->ifx.alt) Expr_destroy(exp->ifx.alt); break;
		case EXP_WHILE: Expr_destroy(exp->whilex.cond); if(exp->whilex.exprHead) DeleteProgram(exp->whilex.exprHead); break;
		case EXP_RETURN: if(exp->retExpr) Expr_destroy(exp->retExpr); break;
		case EXP_UNARY: Expr_destroy(exp->unary.exp); break;
		default: break;
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
			case OP_NEQU:			printf("nequ\n"); break;
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

void InitInterpreter()
{
	ResetMachine();
	NumObjects = 0;
	MaxNumObjects = 8;
}

void DeleteInterpreter()
{
	DeleteVariables();
	DeleteFunctions();
	DeleteForeignFunctions();
	DeleteConstants();
	StackSize = 0;
	VariableAmount = 0;
	GarbageCollect();
	ResetMachine();
}

void CompileFile(FILE* in)
{
	CurTok = 0;
	Expr* prog = ParseProgram(in);
	CompileProgram(prog);
	GenerateCode(OP_HALT);
	DeleteProgram(prog);
}

void RunProgram()
{
	RunMachine();
}

void InterpretFile(FILE* in)
{
	CurTok = 0;
	Expr* prog = ParseProgram(in);
	CompileProgram(prog);
	GenerateCode(OP_HALT);
	// DebugMachineProgram();
	DeleteProgram(prog);

	RunMachine();
}

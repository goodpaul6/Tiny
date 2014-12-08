// tiny -- an bytecode-based interpreter for the tiny language
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

void* emalloc(size_t size)
{
	void* data = malloc(size);
	assert(data && "Out of memory!");
	return data;
}

char* estrdup(char* string)
{
	char* dupString = emalloc(strlen(string) + 1);
	strcpy(dupString, string);
	return dupString;
}

#define MAX_PROG_LEN	2048
#define MAX_CONST_AMT	256
#define MAX_STACK		512
#define MAX_INDIR		1024
#define MAX_VARS		128
#define MAX_FUNCS		128

typedef unsigned char Word;

Word Program[MAX_PROG_LEN];
Word ProgramLength;
int ProgramCounter;
int FramePointer;

double Constants[MAX_CONST_AMT];
int ConstantAmount = 0;

double Stack[MAX_STACK];
int StackSize = 0;

int IndirStack[MAX_INDIR];
int IndirStackSize;

char* VariableNames[MAX_VARS];
double Variables[MAX_VARS];
int VariableAmount = 0;

char* FunctionNames[MAX_FUNCS];
int FunctionPcs[MAX_FUNCS];
int FunctionAmount = 0;

void DeleteVariables()
{
	for(int i = 0; i < VariableAmount; ++i)
		free(VariableNames[i]);
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

int RegisterConstant(double value)
{
	for(int i = 0; i < ConstantAmount; ++i)
	{
		if(Constants[i] == value)
			return i;
	}
	
	assert(ConstantAmount < MAX_CONST_AMT && "Constant Overflow!");
	Constants[ConstantAmount++] = value;
	return ConstantAmount - 1;
}

int RegisterVariableName(char* name)
{
	for(int i = 0; i < VariableAmount; ++i)
	{
		if(strcmp(VariableNames[i], name) == 0)
			return i;
	}
	VariableNames[VariableAmount++] = estrdup(name);
	return VariableAmount - 1;
}

int RegisterFunction(char* name)
{
	for(int i = 0; i < FunctionAmount; ++i)
	{
		if(strcmp(FunctionNames[i], name) == 0)
			return i;
	}
	FunctionNames[FunctionAmount++] = estrdup(name);
	return FunctionAmount - 1;
}

enum 
{
	OP_PUSH,
	OP_POP,
	
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
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
	OP_GOTONZ,

	OP_CALL,
	OP_RETURN,
	OP_RETURN_VALUE,
	
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

void DoPush(double value)
{
	if(StackSize >= MAX_STACK) 
	{
		fprintf(stderr, "Stack Overflow at PC: %i!", ProgramCounter);
		exit(1);
	}
	Stack[StackSize++] = value;
}

double DoPop()
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
	char buffer[32];
	int i = 0;
	int sign = 1;
	int c = getchar();
	if(c == '-') 
	{
		sign = -1;
		c = getchar();
	}
	while(isdigit(c))
	{
		assert(i < 32 - 1 && "Number was too long!");
		buffer[i++] = c;
		c = getchar();
	}
	
	buffer[i] = '\0';
	DoPush(strtod(buffer, NULL) * sign);
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
			double value = Constants[ReadInteger()];
			DoPush(value);
		} break;
		
		case OP_POP:
		{
			DoPop();
			++ProgramCounter;
		} break;
		
		#define BIN_OP(OP, operator) case OP_##OP: { double val2 = DoPop(); double val1 = DoPop(); DoPush(val1 operator val2); ++ProgramCounter; } break;
		
		BIN_OP(ADD, +)
		BIN_OP(SUB, -)
		BIN_OP(MUL, *)
		BIN_OP(DIV, /)
		BIN_OP(LT, <)
		BIN_OP(LTE, <=)
		BIN_OP(GT, >)
		BIN_OP(GTE, >=)
		BIN_OP(EQU, ==)
		BIN_OP(NEQU, !=)
		
		#undef BIN_OP
		
		case OP_PRINT:
		{
			printf("%g\n", DoPop());
			++ProgramCounter;
		} break;

		case OP_SET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			Variables[varIdx] = DoPop();
		} break;
		
		case OP_GET:
		{
			++ProgramCounter;
			int varIdx = ReadInteger();
			DoPush(Variables[varIdx]);
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
			if(DoPop() == 0)
			{	
				ProgramCounter = newPc;
			}
		} break;
		
		case OP_GOTONZ:
		{
			++ProgramCounter;
			int newPc = ReadInteger();
			if(DoPop() != 0)
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
			double retVal = DoPop();
			DoPopIndir();
			DoPush(retVal);
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
			double val = DoPop();
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
	TOK_LOCALIDX = -8,
	TOK_PROC = -9,
	TOK_IF = -10,
	TOK_EQUALS = -11,
	TOK_NOTEQUALS = -12,
	TOK_LTE = -13,
	TOK_GTE = -14,
	TOK_RETURN = -15,
	TOK_WHILE = -16,
	TOK_EOF = -17,
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
		
		if(strcmp(TokenBuffer, "begin") == 0) return TOK_BEGIN;
		if(strcmp(TokenBuffer, "end") == 0) return TOK_END;
		if(strcmp(TokenBuffer, "read") == 0) return TOK_READ;
		if(strcmp(TokenBuffer, "write") == 0) return TOK_WRITE;
		if(strcmp(TokenBuffer, "proc") == 0) return TOK_PROC;
		if(strcmp(TokenBuffer, "if") == 0) return TOK_IF;
		if(strcmp(TokenBuffer, "return") == 0) return TOK_RETURN;
		if(strcmp(TokenBuffer, "while") == 0) return TOK_WHILE;
		
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
		int sign = 1;
		if(last == '-')
		{
			sign = -1;
			last = getc(in);
		}
		
		int i = 0;
		while(isdigit(last))
		{
			assert(i < MAX_TOK_LEN - 1 && "Local index was too long!");
			TokenBuffer[i++] = last;
			last = getc(in);
		}
		TokenBuffer[i] = '\0';
		
		TokenNumber = (int)strtol(TokenBuffer, NULL, 10) * sign;
		return TOK_LOCALIDX;
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
	EXP_LOCALIDX,
	EXP_BINARY,
	EXP_PAREN,
	EXP_PROC,
	EXP_IF,
	EXP_UNARY,
	EXP_RETURN,
	EXP_WHILE,
} ExprType;

#define MAX_READ_WRITE	128
#define MAX_ARGS		16

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
			int numIds;
		} read;
		
		struct
		{
			struct sExpr* exprs[MAX_READ_WRITE];
			int numExprs;
		} write;
		
		int number;
		
		int ident;
		
		struct
		{
			int callee;
			struct sExpr* args[MAX_ARGS];
			int numArgs;
		} call;
		
		int localIdx;
		
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
		} proc;

		struct
		{
			struct sExpr* cond;
			struct sExpr* exprHead;
		} ifx;
		
		struct
		{
			struct sExpr* cond;
			struct sExpr* exprHead;
		} whilex;
		
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
				Expr* exp = Expr_create(EXP_ID);
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
			exp->call.callee = RegisterFunction(ident);
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
			exp->number = RegisterConstant(TokenNumber);
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_LOCALIDX:
		{
			Expr* exp = Expr_create(EXP_LOCALIDX);
			exp->localIdx = (int)TokenNumber;
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_PROC:
		{
			Expr* exp = Expr_create(EXP_PROC);
			
			GetNextToken(in);
			assert(CurTok == TOK_IDENT && "Proc name must be identifier!");
			
			exp->proc.name = RegisterFunction(TokenBuffer);
			
			GetNextToken(in);
			if(CurTok != TOK_END)
			{
				Expr* curExp = ParseExpr(in);
				Expr* head = curExp;
				
				while(CurTok != TOK_END)
				{
					curExp->next = ParseExpr(in);
					curExp = curExp->next;
				}
				exp->proc.exprHead = head;
			}
			else
				exp->proc.exprHead = NULL;
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_IF:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_IF);
			exp->ifx.cond = ParseExpr(in);
			if(CurTok != TOK_END)
			{
				Expr* curExp = ParseExpr(in);
				Expr* head = curExp;
				
				while(CurTok != TOK_END)
				{
					curExp->next = ParseExpr(in);
					curExp = curExp->next;
				}
				exp->ifx.exprHead = head;
			}
			else
				exp->ifx.exprHead = NULL;
			GetNextToken(in);
			return exp;
		} break;
		
		case TOK_WHILE:
		{
			GetNextToken(in);
			Expr* exp = Expr_create(EXP_WHILE);
			exp->whilex.cond = ParseExpr(in);
			if(CurTok != TOK_END)
			{
				Expr* curExp = ParseExpr(in);
				Expr* head = curExp;
				
				while(CurTok != TOK_END)
				{
					curExp->next = ParseExpr(in);
					curExp = curExp->next;
				}
				exp->whilex.exprHead = head;
			}
			else
				exp->whilex.exprHead = NULL;
			
			GetNextToken(in);
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
				assert(CurTok == TOK_IDENT && "Expected identifier in list for read expression!");
				exp->read.ids[exp->read.numIds++] = RegisterVariableName(TokenBuffer); 
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
	while(CurTok != TOK_BEGIN)
		GetNextToken(in);
	GetNextToken(in);
	
	if(CurTok != TOK_END)
	{
		Expr* head = ParseExpr(in);
		Expr* exp = head;
		
		while(CurTok != TOK_END)
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
			printf("%s", VariableNames[exp->ident]);
		} break;
		
		case EXP_CALL:
		{
			printf("%s(", FunctionNames[exp->call.callee]);
			for(int i = 0; i < exp->call.numArgs; ++i)
			{
				PrintExpr(exp->call.args[i]);
				if(i + 1 < exp->call.numArgs) printf(",");
			}
			printf(")");
		} break;
		
		case EXP_NUM:
		{
			printf("%g", Constants[exp->number]);
		} break; 
		
		case EXP_READ:
		{
			printf("read ");
			for(int i = 0; i < exp->read.numIds; ++i)
				printf("%s ", VariableNames[exp->read.ids[i]]);
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
		
		case EXP_LOCALIDX:
		{
			printf("$%i", exp->localIdx);
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
			if(exp->ifx.exprHead)
				PrintProgram(exp->ifx.exprHead);
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
			GenerateCode(OP_GET);
			GenerateInt(exp->ident);
		} break;
		
		case EXP_CALL:
		{
			for(int i = 0; i < exp->call.numArgs; ++i)
				CompileExpr(exp->call.args[i]);
			GenerateCode(OP_CALL);
			GenerateInt(exp->call.numArgs);
			GenerateInt(exp->call.callee);
		} break;
		
		case EXP_NUM:
		{
			GenerateCode(OP_PUSH);
			GenerateInt(exp->number);
		} break; 
		
		case EXP_LOCALIDX:
		{
			GenerateCode(OP_GETLOCAL);
			GenerateInt(exp->localIdx);
		} break;
		
		case EXP_READ:
		{
			for(int i = 0; i < exp->read.numIds; ++i)
			{	
				GenerateCode(OP_READ);
				GenerateCode(OP_SET);
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
					}
					else if(exp->binary.lhs->type == EXP_LOCALIDX)
					{
						CompileExpr(exp->binary.rhs);
						GenerateCode(OP_SETLOCAL);
						GenerateInt(exp->binary.lhs->localIdx);
					}
					else
					{
						fprintf(stderr, "LHS of assignment operation must be an id or a local index $x\n");
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
				
				case ':':
				{
					CompileExpr(exp->binary.lhs);
					CompileExpr(exp->binary.rhs);
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
					GenerateInt(RegisterConstant(-1));
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
			if(exp->ifx.exprHead)
				CompileProgram(exp->ifx.exprHead);
			GenerateIntAt(ProgramLength, skipGotoPc);
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
		case EXP_ID: case EXP_NUM:	break;
		
		case EXP_CALL: 
		{
			for(int i = 0; i < exp->call.numArgs; ++i)
				Expr_destroy(exp->call.args[i]);
		} break;
		
		case EXP_READ: break;
		case EXP_WRITE: for(int i = 0; i < exp->write.numExprs; ++i) Expr_destroy(exp->write.exprs[i]); break;
		case EXP_BINARY: Expr_destroy(exp->binary.lhs); Expr_destroy(exp->binary.rhs); break;
		case EXP_PAREN: Expr_destroy(exp->paren); break;
		case EXP_PROC: if(exp->proc.exprHead) DeleteProgram(exp->proc.exprHead); break;
		case EXP_IF: if(exp->ifx.exprHead) DeleteProgram(exp->ifx.exprHead); break;
		case EXP_WHILE: if(exp->whilex.exprHead) DeleteProgram(exp->whilex.exprHead); break;
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
			case OP_PUSH:			printf("push\n"); i += 4; break;
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
			case OP_GOTONZ:			printf("gotonz\n"); i += 4; break;
			case OP_CALL:			printf("call\n"); i += 8; break;
			case OP_RETURN:			printf("return\n"); break;
			case OP_RETURN_VALUE:	printf("return_value\n"); break;
			case OP_GETLOCAL:		printf("getlocal\n"); i += 4; break;
			case OP_SETLOCAL:		printf("setlocal\n"); i += 4; break;
			case OP_HALT:			printf("halt\n");
		}
	}
}

int main(int argc, char* argv[])
{
	ResetMachine();
	
	FILE* input = stdin;
	
	if(argc >= 2)
	{
		input = fopen(argv[1], "r");
		assert(input && "Failed to open input file!");
	}
	
	Expr* exp = ParseProgram(input);
	if(exp)
	{
		CompileProgram(exp);
		GenerateCode(OP_HALT);
		DeleteProgram(exp);
		if(argc == 3)
			DebugMachineProgram();
	
		RunMachine();
	
		DeleteVariables();
		DeleteFunctions();
	}
	if(argc == 2)
		fclose(input);
	
	return 0;
}

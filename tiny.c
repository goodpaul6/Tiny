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
#define MAX_STACK		128
#define MAX_VARS		128

typedef unsigned char Word;

Word Program[MAX_PROG_LEN];
Word ProgramLength;
Word ProgramCounter;

double Constants[MAX_CONST_AMT];
int ConstantAmount = 0;

double Stack[MAX_STACK];
int StackSize = 0;

char* VariableNames[MAX_VARS];
double Variables[MAX_VARS];
int VariableAmount = 0;

void DeleteVariables()
{
	for(int i = 0; i < VariableAmount; ++i)
		free(VariableNames[i]);
	VariableAmount = 0;
}

void ResetMachine()
{
	ProgramLength = 0;
	ProgramCounter = -1;
	ConstantAmount = 0;
	StackSize = 0;
	VariableAmount = 0;
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

enum 
{
	OP_PUSH,
	OP_POP,
	
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	
	OP_PRINT,
	
	OP_SET,
	OP_GET,
	
	OP_READ,
	
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
	assert(StackSize < MAX_STACK && "Stack Overflow!");
	Stack[StackSize++] = value;
}

double DoPop()
{
	assert(StackSize > 0 && "Stack Underflow!");
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
	TOK_EOF = -8
};

#define MAX_TOK_LEN		256
char TokenBuffer[MAX_TOK_LEN];
double TokenNumber;

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
	
	if(last == EOF)
		return TOK_EOF;
		
	int lastChar = last;
	last = getc(in);
	return lastChar;
}

typedef enum
{
	EXP_PROGRAM,
	EXP_READ,
	EXP_WRITE,
	EXP_ID,
	EXP_NUM,
	EXP_BINARY,
	EXP_PAREN,
	EXP_UNARY
} ExprType;

#define MAX_READ_WRITE	128

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
			Expr* exp = Expr_create(EXP_ID);
			exp->ident = RegisterVariableName(TokenBuffer);
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

Expr* ParseBinRhs(FILE* in, Expr* lhs)
{
	int op = CurTok;
	
	if(CurTok != '+' && CurTok != '-' && CurTok != '*' && CurTok != '/' && CurTok != '=')
		return lhs;
		
	GetNextToken(in);
	
	if(op == '=')
	{
		Expr* rhs = ParseExpr(in);
		Expr* exp = Expr_create(EXP_BINARY);
		exp->binary.op = op;
		exp->binary.lhs = lhs;
		exp->binary.rhs = rhs;
		return exp;
	}
	
	Expr* rhs = ParseFactor(in);
	
	if(CurTok == '+' || CurTok == '-' || CurTok == '*' || CurTok == '/')
	{
		Expr* exp = Expr_create(EXP_BINARY);
		exp->binary.op = op;
		exp->binary.lhs = lhs;
		exp->binary.rhs = rhs;
		return ParseBinRhs(in, exp);
	}
	
	Expr* exp = Expr_create(EXP_BINARY);
	exp->binary.op = op;
	exp->binary.lhs = lhs;
	exp->binary.rhs = rhs;
	return exp;
}

Expr* ParseExpr(FILE* in)
{
	Expr* factor = ParseFactor(in);
	return ParseBinRhs(in, factor);
}

Expr* ParseStatement(FILE* in)
{
	if(CurTok == TOK_READ)
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
	}
	
	if(CurTok == TOK_WRITE)
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
	}
	
	return ParseExpr(in);
}

Expr* ParseProgram(FILE* in)
{
	while(CurTok != TOK_BEGIN)
		GetNextToken(in);
	GetNextToken(in);
	
	Expr* head = Expr_create(EXP_PROGRAM);
	Expr* exp = head;
	
	while(CurTok != TOK_END)
	{
		Expr* stmt = ParseStatement(in);
		head->next = stmt;
		head = stmt;
	}	
	return exp;
}

void PrintExpr(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID:
		{	
			printf("%s", VariableNames[exp->ident]);
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
		
		default:
		{
			printf("cannot print expression type %i\n", exp->type);
		} break;
	}
}

void PrintProgram(Expr* program)
{
	Expr* exp = program->next;
	printf("begin\n");
	while(exp)
	{
		PrintExpr(exp);
		exp = exp->next;
	}
	printf("\nend\n");
}

void CompileExpr(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID:
		{
			GenerateCode(OP_GET);
			GenerateInt(exp->ident);
		} break;
		
		case EXP_NUM:
		{
			GenerateCode(OP_PUSH);
			GenerateInt(exp->number);
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
					assert(exp->binary.lhs->type == EXP_ID && "Left hand side of assignment expression must be an identifier");
					CompileExpr(exp->binary.rhs);
					GenerateCode(OP_SET);
					GenerateInt(exp->binary.lhs->ident);
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
	}
}

void CompileProgram(Expr* program)
{
	Expr* exp = program->next;
	while(exp)
	{
		CompileExpr(exp);
		exp = exp->next;
	}
	GenerateCode(OP_HALT);
}

void Expr_destroy(Expr* exp)
{
	switch(exp->type)
	{
		case EXP_ID: case EXP_NUM:	break;
		case EXP_READ: break;
		case EXP_WRITE: for(int i = 0; i < exp->write.numExprs; ++i) Expr_destroy(exp->write.exprs[i]); break;
		case EXP_BINARY: Expr_destroy(exp->binary.lhs); Expr_destroy(exp->binary.rhs); break;
	}
	free(exp);
}

void DeleteProgram(Expr* program)
{
	Expr* exp = program->next;
	while(exp)
	{
		Expr* next = exp->next;
		Expr_destroy(exp);
		exp = next;
	}
	Expr_destroy(program);
}

int main(int argc, char* argv[])
{
	ResetMachine();
	
	FILE* input = stdin;
	
	if(argc == 2)
	{
		input = fopen(argv[1], "r");
		assert(input && "Failed to open input file!");
	}
	
	Expr* exp = ParseProgram(input);
	CompileProgram(exp);
	DeleteProgram(exp);
	
	RunMachine();
	
	DeleteVariables();
	
	return 0;
}

#include <assert.h>
#include <string.h>
#include <math.h>

#include "context.c"
#include "common.c"
#include "map.c"
#include "stringpool.c"
#include "type.c"
#include "lexer.c"
#include "symbols.c"
#include "ast.c"
#include "parser.c"

int main(int argc, char** argv)
{
    const char* s =  
        "x := 10.0\n"
        "y := 20.0\n"
        "func add_xy(a: float, b: float): float {\n"
        "   return a + b + x + y; \n"
        "}\n";

    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    float* numbers = NULL;
    INIT_BUF(numbers, ctx);

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, ctx);

    TypetagPool tp;
    InitTypetagPool(&tp, ctx);

    Symbols s;
    InitSymbols(&s, ctx, &sp, &tp);

    Parser p;
    InitParser(&p, ctx, "test_parser.c", s, strlen(s), &sp, &sym, &tp, &numbers);
    
    AST** asts = ParseProgram(&p);

    assert(asts[0]->type == AST_BINARY);
    assert(asts[0]->binary.op == TOK_DECLARE);
    assert(asts[0]->binary.lhs->type == AST_ID);
    assert(asts[0]->binary.rhs->type == AST_FLOAT);
    assert(fabsf(numbers[asts[0]->binary.rhs->fIndex] - 10.0f) < 0.002f);

    assert(asts[1]->type == AST_BINARY);
    assert(asts[1]->binary.op == TOK_DECLARE);
    assert(asts[1]->binary.lhs->type == AST_ID);
    assert(asts[1]->binary.rhs->type == AST_FLOAT);
    assert(fabsf(numbers[asts[1]->binary.rhs->fIndex] - 20.0f) < 0.002f);

    return 0;
}

#include <math.h>
#include <assert.h>

#include "context.c"
#include "common.c"
#include "lexer.c"

#ifdef NDEBUG
#error "Undef NDEBUG for tests"
#endif

int main(int argc, char** argv)
{
    const char* s = "( ) { } + - * / % > < = ! & | , ; : . && || := :: += -= *= /= %= |= &= == != <= >= null true false 'a' 1 1.5 \"hello, world\" abcd_1234 if else while for return func foreign struct new cast break continue @";

    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    Lexer lexer;
    InitLexer(&lexer, &ctx, "lexer.c", s, strlen(s));

#define EXPECT(tok) assert(GetToken(&lexer) == (tok))

    EXPECT(TOK_OPENPAREN);
    EXPECT(TOK_CLOSEPAREN);
    EXPECT(TOK_OPENCURLY);
    EXPECT(TOK_CLOSECURLY);

    EXPECT(TOK_PLUS);
    EXPECT(TOK_MINUS);
    EXPECT(TOK_STAR);
    EXPECT(TOK_SLASH);
    EXPECT(TOK_PERCENT);
    EXPECT(TOK_GT);
    EXPECT(TOK_LT);
    EXPECT(TOK_EQUAL);
    EXPECT(TOK_BANG);
    EXPECT(TOK_AND);
    EXPECT(TOK_OR);
    EXPECT(TOK_COMMA);
    EXPECT(TOK_SEMI);
    EXPECT(TOK_COLON);
    EXPECT(TOK_DOT);

    EXPECT(TOK_LOG_AND);
    EXPECT(TOK_LOG_OR);

    EXPECT(TOK_DECLARE);
    EXPECT(TOK_DECLARECONST);

    EXPECT(TOK_PLUSEQUAL);
    EXPECT(TOK_MINUSEQUAL);
    EXPECT(TOK_STAREQUAL);
    EXPECT(TOK_SLASHEQUAL);
    EXPECT(TOK_PERCENTEQUAL);
    EXPECT(TOK_OREQUAL);
    EXPECT(TOK_ANDEQUAL);

    EXPECT(TOK_EQUALS);
    EXPECT(TOK_NOTEQUALS);
    EXPECT(TOK_LTE);
    EXPECT(TOK_GTE);

    EXPECT(TOK_NULL);

    EXPECT(TOK_BOOL);
    assert(lexer.bValue == true);
    EXPECT(TOK_BOOL);
    assert(lexer.bValue == false);

    EXPECT(TOK_CHAR);
    assert(lexer.iValue == 'a');

    EXPECT(TOK_INT);
    assert(lexer.iValue == 1);

    EXPECT(TOK_FLOAT);
    assert(fabsf(lexer.fValue - 1.5f) < 0.001f);

    EXPECT(TOK_STRING);
    assert(strcmp(lexer.lexeme, "hello, world") == 0);

    EXPECT(TOK_IDENT);
    assert(strcmp(lexer.lexeme, "abcd_1234") == 0);

    EXPECT(TOK_IF);
    EXPECT(TOK_ELSE);
    EXPECT(TOK_WHILE);
    EXPECT(TOK_FOR);
    EXPECT(TOK_RETURN);
    EXPECT(TOK_FUNC);
    EXPECT(TOK_FOREIGN);
    EXPECT(TOK_STRUCT);
    EXPECT(TOK_NEW);
    EXPECT(TOK_CAST);
	EXPECT(TOK_BREAK);
	EXPECT(TOK_CONTINUE);

    EXPECT(TOK_ERROR);

    DestroyLexer(&lexer);

    return 0;
}

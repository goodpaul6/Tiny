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
		"x := add(null, false, true, 'a', 10, 20.0, \"hello\",\n"
		"         10 + (20 * 30) - 40 > 10)\n"
		"{\n"
		"   test(1, 3)\n"
		"   test(1, 2)\n"
		"}\n"
		"func test(x: int, y: int) :int {\n"
		"   if x < -(10 + 20) || y > 10 {\n"
		"       return x + y\n"
		"   } else return 0\n"
		"   return;\n"
		"}\n"
		"while true {\n"
		"   for i := 0; i < 1000; i += 1 {\n"
		"       print(cast(i.x.y.z, int))\n"
		"       i.x.y.z = new Struct{i * 1000}\n"
		"   }\n"
		"}\n"
		"struct Struct {\n"
		"   x: int\n"
		"}\n";


    Tiny_Context ctx = { NULL, Tiny_DefaultAlloc };

    float* numbers = NULL;
    INIT_BUF(numbers, &ctx);

    Tiny_StringPool sp;
    Tiny_InitStringPool(&sp, &ctx);

    TypetagPool tp;
    InitTypetagPool(&tp, &ctx);

	Symbols sym;
    InitSymbols(&sym, &ctx, &sp, &tp);

    Parser p;
    InitParser(&p, &ctx, "test_parser.c", s, strlen(s), &sp, &sym, &tp, &numbers);
    
    AST** asts = ParseProgram(&p);

	assert(asts);

    // TODO(Apaar): Find a good way to confirm everything is of the expected type
    // Maybe make a tree of types and then recursively visit the ast confirming everything
    // is as expected.

    DestroyParser(&p);

    return 0;
}

#include <assert.h>
#include <stdio.h>

#include "tiny.c"

int main(int argc, char** argv) {
    Tiny_Context ctx = {NULL, Tiny_DefaultAlloc};

    Parser parser;
    Resolver resolver;

    InitParser(&parser, &ctx);
    InitResolver(&resolver, &ctx, &parser.sp, &parser.sym, &parser.tp);

    const char* code = "func add(x: int, y: int): int { return x + y } add(10, 20)";

    bool res = Parse(&parser, "test", code, strlen(code));

    assert(res);

    res = ResolveProgram(&resolver, parser.asts);

    assert(res);

    const char* invalidCode = "func add(x: int, y: int): int { return x + y } add(10, 10.5)";

    res = Parse(&parser, "test2", invalidCode, strlen(invalidCode));

    assert(res);

    res = ResolveProgram(&resolver, parser.asts);

    assert(!res);

    assert(strcmp(resolver.errorMessage,
                  "Argument 2 is supposed to be a int but you supplied a float.") == 0);

    return 0;
}

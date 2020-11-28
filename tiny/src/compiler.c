#include <setjmp.h>

typedef struct Compiler {
    Tiny_Context* ctx;
    Tiny_State* state;

    // If this is NULL but Compile returned false, then there's probably
    // a parser or lexer error.
    char* errorMessage;

    jmp_buf topLevelEnv;
} Compiler;

static void InitCompiler(Compiler* comp) {}

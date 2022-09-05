#pragma once

// Since the language is statically typed, the VM operates with typed instructions.
// However, "any" values are represented with a single byte that represents their
// dynamic type followed by their data. Varargs are always passed in as 'any'

// Ok so it turns out now there's no such thing as null for primitive types. But that's
// a good thing! Objects are going to be the only reference types now. Maybe even value
// semantics; then reference semantics can be done using a 'ref' type maybe. Too much for
// now though. Stick to reference semantics for struct instances.

// Note that the VM does not actually store types anywhere. This could pose a security
// risk if you are loading untrusted bytecode directly. As such, you should avoid doing
// so, and instead load Tiny source code. This ensures the appropriate type checking
// is done at compile time.

// Notice that this header defines value types. These would be an implementation
// detail were it not for the fact that the "any" type exists.

#include "state.h"

#ifndef TINY_THREAD_STACK_SIZE
#define TINY_THREAD_STACK_SIZE 256
#endif

#ifndef TINY_THREAD_MAX_CALL_DEPTH
#define TINY_THREAD_MAX_CALL_DEPTH 64
#endif

typedef struct Tiny_StringPool Tiny_StringPool;

typedef struct Tiny_State Tiny_State;

typedef struct Tiny_Object Tiny_Object;

// TODO(Apaar): All string creation incurs an object allocation
// because we don't distinguish between strings and other types
// of roots, so they're all boxed. However, in the future, we
// can distinguish the two or add a "static string" type
// which basically doesn't participate the GC process. It is interned
// once and then remains there forever. Useful for constants and stuff.
typedef enum Tiny_ValueType {
    TINY_VAL_BOOL,
    TINY_VAL_CHAR,
    TINY_VAL_INT,
    TINY_VAL_FLOAT,
    TINY_VAL_STRING
} Tiny_ValueType;

typedef union {
    bool b;
    uint32_t c;
    int i;
    float f;
    Tiny_Object* o;
} Tiny_Value;

typedef struct Tiny_Frame {
    uint8_t* pc;
    Tiny_Value* fp;
    uint8_t nargs;

    Tiny_LocalRoots roots;
} Tiny_Frame;

typedef struct Tiny_VM {
    Tiny_Context* ctx;
    const Tiny_State* state;

    // Multiple VMs can share the same StringPool. However,
    // since StringPool is not thread-safe, all the VMs which
    // share it must execute on the same OS thread or their execution
    // should be synchronized.
    Tiny_StringPool* stringPool;

    // The garbage collection and heap is thread-local
    Tiny_Object* gcHead;
    int numObjects;
    int maxNumObjects;

    // Global vars are owned by each vm
    Tiny_Value* globals;

    uint8_t* pc;
    Tiny_Value* sp;
    Tiny_Value* fp;

    // Roots of the function being executed currently
    //
    // I explicitly copy this because if the state->localRoots
    // buffer is appended to (e.g. if we compile more code)
    // then that will invalidate any pointers to LocalRoots objects.
    // However, we don't care about the LocalRoots object, we just
    // care about the indices inside it, so this works.
    Tiny_LocalRoots roots;

    // Last returned value
    Tiny_Value retVal;

    Tiny_Value stack[TINY_THREAD_STACK_SIZE];

    int fc;
    Tiny_Frame frames[TINY_THREAD_MAX_CALL_DEPTH];

    // These keep track of what file/line of source code
    // the instruction at the current PC originated from
    const char* fileName;
    int lineNumber;

    // Userdata pointer. Set to NULL when Init is called. Use it for whatever you want
    void* userdata;
} Tiny_VM;

void Tiny_InitVM(Tiny_VM* vm, Tiny_Context* ctx, const Tiny_State* state, Tiny_StringPool* sp);
void Tiny_Run(Tiny_VM* vm);
void Tiny_DestroyVM(Tiny_VM* vm);

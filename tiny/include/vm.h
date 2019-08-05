#pragma once

// Since the language is statically typed, the VM operates with typed instructions.
// However, "any" values are represented with a single byte that represents their
// dynamic type followed by their data. Varargs are always passed in as 'any'

// Ok so it turns out now there's no such thing as null for primitive types. But that's
// a good thing! Objects are going to be the only reference types now. Maybe even value
// semantics; then reference semantics can be done using a 'ref' type maybe. Too much for
// now though. Stick to reference semantics for struct instances.

#ifndef TINY_THREAD_STACK_SIZE
#define TINY_THREAD_STACK_SIZE  256
#endif

#ifndef TINY_THREAD_MAX_CALL_DEPTH
#define TINY_THREAD_MAX_CALL_DEPTH  64
#endif

typedef struct Tiny_StringPool Tiny_StringPool;

typedef struct Tiny_State Tiny_State;

typedef struct Tiny_Object Tiny_Object;

typedef enum Tiny_ValueType
{
    TINY_VAL_NULL,
    TINY_VAL_BOOL,
    TINY_VAL_CHAR,
    TINY_VAL_INT,
    TINY_VAL_FLOAT,
    TINY_VAL_STRING,
    TINY_VAL_POINTER
} Tiny_ValueType;

typedef union {
    bool b;
    char c;
    int i;
    float f;
    const char* s;
    void* p;
} Tiny_Value;

typedef struct Tiny_Frame
{
    uint8_t* pc;
    char* fp;
    uint8_t nargs;
} Tiny_Frame;

typedef struct Tiny_StateThread
{
    Tiny_Context* ctx;
    const Tiny_State* state;

    // Multiple threads could share the same StringPool; however,
    // since StringPool is not thread-safe, all the StateThreads which
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
    char* sp;
    char* fp;
    
    // Last returned value
    Tiny_Value retVal;

    Tiny_Value stack[TINY_THREAD_STACK_SIZE];

    int fc;
    Tiny_Frame frames[TINY_THREAD_MAX_CALL_DEPTH];
     
    // These keep track of what file/line of source code
    // the instruction at the current PC originated from
    const char* fileName;
	int lineNumber;

    // Userdata pointer. Set to NULL when InitThread is called. Use it for whatever you want
    void* userdata;
} Tiny_VM;

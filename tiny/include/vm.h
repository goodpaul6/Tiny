#pragma once

// Since the language is statically typed, the VM operates with typed instructions.
// However, "any" values are represented with a single byte that represents their
// dynamic type followed by their data. Varargs are always passed in as 'any'

// Ok so it turns out now there's no such thing as null for primitive types. But that's
// a good thing! Objects are going to be the only reference types now. Maybe even value
// semantics; then reference semantics can be done using a 'ref' type maybe. Too much for
// now though. Stick to reference semantics for struct instances.

#include "stringpool.h"

#ifndef TINY_THREAD_STACK_SIZE
#define TINY_THREAD_STACK_SIZE  1024
#endif

#ifndef TINY_THREAD_MAX_CALL_DEPTH
#define TINY_THREAD_MAX_CALL_DEPTH  64
#endif

typedef struct Tiny_State Tiny_State;

typedef struct Tiny_Object Tiny_Object;

typedef const char** Tiny_ArgPtr;

typedef enum Tiny_ValueType
{
    TINY_VAL_NULL,
    TINY_VAL_BOOL,
    TINY_VAL_CHAR,
    TINY_VAL_INT,
    TINY_VAL_FLOAT,
    TINY_VAL_STRING,
    TINY_VAL_NATIVE,
    TINY_VAL_LIGHT_NATIVE,
    TINY_VAL_STRUCT
} Tiny_ValueType;

typedef struct Tiny_Frame
{
    int pc;
    char* fp;
    char* spBeforeArgs;
} Tiny_Frame;

typedef struct Tiny_StateThread
{
    Tiny_Context* ctx;
    Tiny_StringPool stringPool;    

    const Tiny_State* state;

    // The garbage collection and heap is thread-local 
    Tiny_Object* gcHead;
    int numObjects;
    int maxNumObjects;

    // Global vars are owned by each thread
    char* globals;

    char* pc;
    char* sp;
    char* fp;

    char stack[TINY_THREAD_STACK_SIZE];

    int fc;
    Tiny_Frame frames[TINY_THREAD_MAX_CALL_DEPTH];
 
    // These keep track of what file/line of source code
    // the instruction at the current PC originated from
    const char* fileName;
	int lineNumber;

    // Userdata pointer. Set to NULL when InitThread is called. Use it for whatever you want
    void* userdata;
} Tiny_StateThread;

// Basically when the user is given a function and they have to retrieve arguments out, they
// must do so in order.

void Tiny_InitThread(Tiny_StateThread* thread, Tiny_Context* ctx, const Tiny_State* state);

void Tiny_PushBool(Tiny_StateThread* thread, bool b);
void Tiny_PushChar(Tiny_StateThread* thread, char c);
void Tiny_PushInt(Tiny_StateThread* thread, int i);
void Tiny_PushFloat(Tiny_StateThread* thread, float f);
void Tiny_PushString(Tiny_StateThread* thread, const char* str, size_t len);

// TODO(Apaar): Figure out how native pointers are tagged
void Tiny_PushNative(Tiny_StateThread* thread, void* p, const void* tag);

bool Tiny_ReadBool(Tiny_ArgPtr p);
char Tiny_ReadChar(Tiny_ArgPtr p);
int Tiny_ReadInt(Tiny_ArgPtr p);
float Tiny_ReadFloat(Tiny_ArgPtr p);
const char* Tiny_ReadString(Tiny_ArgPtr p);
void* Tiny_ReadNative(Tiny_ArgPtr p, const void** tag);
void* Tiny_ReadLightNative(Tiny_ArgPtr p);

void Tiny_CallFunction(Tiny_StateThread* thread);

void Tiny_Run(Tiny_StateThread* thread);

void Tiny_DestroyThread(Tiny_StateThread* thread);

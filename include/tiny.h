#ifndef TINY_H
#define TINY_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef TINY_THREAD_STACK_SIZE
#define TINY_THREAD_STACK_SIZE  128
#endif

#ifndef TINY_THREAD_MAX_CALL_DEPTH
#define TINY_THREAD_MAX_CALL_DEPTH  64
#endif

typedef struct Tiny_Object Tiny_Object;
typedef struct Tiny_State Tiny_State;

struct Tiny_Value;

// Stores properties about a native
// object. This should be statically allocated
// and only one should exist for each type of
// Native value.
typedef struct
{
    const char* name;

    void(*protectFromGC)(void*);
    void(*finalize)(void*);				
} Tiny_NativeProp;

typedef enum
{
    TINY_VAL_NULL,
    TINY_VAL_BOOL,
    TINY_VAL_INT,
    TINY_VAL_FLOAT,
    TINY_VAL_STRING,
    TINY_VAL_CONST_STRING,
    TINY_VAL_NATIVE,
    TINY_VAL_LIGHT_NATIVE,
    TINY_VAL_STRUCT
} Tiny_ValueType;

typedef struct Tiny_Value
{
    union
    {
        bool boolean;
        int i;
        float f;
        const char* cstr;   // for TINY_VAL_CONST_STRING
        void* addr;         // for TINY_VAL_LIGHT_NATIVE
        Tiny_Object* obj;
    };

    uint8_t type;
} Tiny_Value;

typedef struct Tiny_Frame
{
    int pc, fp;
    int nargs;
} Tiny_Frame;

typedef struct Tiny_StateThread
{
    // Each thread stores a reference
    // to its state
    const Tiny_State* state;

    // The garbage collection and heap is thread-local 
    Tiny_Object* gcHead;
    int numObjects;
    int maxNumObjects;

    // Global vars are owned by each thread
    Tiny_Value* globalVars;

    int pc, fp, sp;
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
} Tiny_StateThread;

typedef Tiny_Value (*Tiny_ForeignFunction)(Tiny_StateThread* thread, const Tiny_Value* args, int count);

#define TINY_FOREIGN_FUNCTION(name) Tiny_Value name(Tiny_StateThread* thread, const Tiny_Value* args, int count)

extern const Tiny_Value Tiny_Null;

void Tiny_ProtectFromGC(Tiny_Value value);

Tiny_Value Tiny_NewBool(bool value);
Tiny_Value Tiny_NewInt(int i);
Tiny_Value Tiny_NewFloat(float f);
Tiny_Value Tiny_NewConstString(const char* string);
Tiny_Value Tiny_NewLightNative(void* ptr);
Tiny_Value Tiny_NewString(Tiny_StateThread* thread, char* string);
Tiny_Value Tiny_NewNative(Tiny_StateThread* thread, void* ptr, const Tiny_NativeProp* prop);

#define Tiny_IsNull(value) (value.type == TINY_VAL_NULL)

static inline bool Tiny_ToBool(const Tiny_Value value)
{
    if(value.type != TINY_VAL_BOOL) return false;
    return value.boolean;
}

static inline int Tiny_ToInt(const Tiny_Value value)
{
    if(value.type != TINY_VAL_INT) return 0;
    return value.i;
}

static inline float Tiny_ToFloat(const Tiny_Value value)
{
    if(value.type != TINY_VAL_FLOAT) return 0;
    return value.f;
}

static inline float Tiny_ToNumber(const Tiny_Value value)
{
	if (value.type == TINY_VAL_FLOAT) return value.f;
	if (value.type != TINY_VAL_INT) return 0;

	return (float)value.i;
}

// Returns NULL if the value isn't a string/const string
const char* Tiny_ToString(const Tiny_Value value);

// Returns value.addr if its a LIGHT_NATIVE
// Returns the normal native address otherwise
void* Tiny_ToAddr(const Tiny_Value value);

// This returns NULL if the value is a LIGHT_NATIVE instead of a NATIVE
// It would also return NULL if the NativeProp supplied when the object was created was NULL,
// either way, you have no information, so deal with it.
const Tiny_NativeProp* Tiny_GetProp(const Tiny_Value value);

// Returns Tiny_Null if value isn't a struct.
// Asserts if index is out of bounds
Tiny_Value Tiny_GetField(const Tiny_Value value, int index);

Tiny_State* Tiny_CreateState(void);

// Exposes an opaque type of the given name.
// The same typename can be registered multiple times, but it will only be defined once.
void Tiny_RegisterType(Tiny_State* state, const char* name);

void Tiny_BindFunction(Tiny_State* state, const char* sig, Tiny_ForeignFunction func);

void Tiny_BindConstBool(Tiny_State* state, const char* name, bool b);
void Tiny_BindConstInt(Tiny_State* state, const char* name, int i);
void Tiny_BindConstFloat(Tiny_State* state, const char* name, float f);
void Tiny_BindConstString(Tiny_State* state, const char* name, const char* value);

void Tiny_CompileString(Tiny_State* state, const char* name, const char* string);
void Tiny_CompileFile(Tiny_State* state, const char* filename);

void Tiny_DeleteState(Tiny_State* state);

void Tiny_InitThread(Tiny_StateThread* thread, const Tiny_State* state);

// Sets the PC of the thread to the entry point of the program
// and allocates space for global variables if they're not already
// allocated
// Requires that state is compiled
void Tiny_StartThread(Tiny_StateThread* thread);

// Returns -1 if the global doesn't exist
// Do note that this will return -1 for global constants as well (those are inlined wherever they are used, so they don't really exist)
int Tiny_GetGlobalIndex(const Tiny_State* state, const char* name);

// Returns -1 if the function doesn't exist
int Tiny_GetFunctionIndex(const Tiny_State* state, const char* name);

// Returns the thread->globalVars[globalIndex] (asserts if index < 0)
// You must have started the thread or called Tiny_CallFunction for this to work
// because otherwise, the thread's global variables might not be allocated.
// Don't worry, this will assert that they have.
Tiny_Value Tiny_GetGlobal(const Tiny_StateThread* thread, int globalIndex);

// Sets a global variable at the given index to the given value (asserts if index < 0)
// You must have started the thread or called Tiny_CallFunction for this to work
// because otherwise, the thread's global variables might not be allocated.
// Don't worry, this will assert that they have.
void Tiny_SetGlobal(Tiny_StateThread* thread, int globalIndex, Tiny_Value value);

// Runs the thread until the function exits and returns the retVal.
// functionIndex can be retrieved using Tiny_GetFunctionIndex.
// The only requirement is that the thread has been initialized.
// You can even call this from a foreign function. It keeps track of the
// state of the thread prior to the function call and restores it afterwards.
// This also allocates globals if the thread hasn't been started already, and in that case, once
// the function call is over, the thread will be "done".
Tiny_Value Tiny_CallFunction(Tiny_StateThread* thread, int functionIndex, const Tiny_Value* args, int count);

static inline bool Tiny_IsThreadDone(const Tiny_StateThread* thread)
{
    return thread->pc < 0;
}

// Run a single cycle of the thread.
// Could potentially trigger garbage collection
// at the end of the cycle.
// Returns whether the cycle was executed or not.
bool Tiny_ExecuteCycle(Tiny_StateThread* thread);

// Gives access to fast dynamically allocated array type.
// Requires tinystd.c and array.h/array.c
void Tiny_BindStandardArray(Tiny_State* state);

// Gives access to fast dictionary type.
// Requires tinystd.c and dict.h/dict.c
void Tiny_BindStandardDict(Tiny_State* state);

// Gives access to a variety of IO functions.
// Requires tinystd.c
void Tiny_BindStandardIO(Tiny_State* state);

// Provides general functions ala stdlib.h
// Requires tinystd.c
void Tiny_BindStandardLib(Tiny_State* state);

void Tiny_DestroyThread(Tiny_StateThread* thread);

#endif


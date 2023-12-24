#ifndef TINY_H
#define TINY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef TINY_THREAD_STACK_SIZE
#define TINY_THREAD_STACK_SIZE 128
#endif

#ifndef TINY_THREAD_MAX_CALL_DEPTH
#define TINY_THREAD_MAX_CALL_DEPTH 64
#endif

// This function should be able to handle all of `malloc`,
// `realloc`, and `free`:
//
// malloc => ptr is NULL, size is provided
// realloc => ptr is not NULL, size is provided
// free => ptr is not NULL, size is 0
typedef void *(*Tiny_AllocFunction)(void *ptr, size_t size, void *userdata);

// You can provide this when you create a Tiny_State
// to override how memory is allocated within the Tiny
// library.
//
// Note that the userdata pointer here is non-const. That
// means if you have context that is shared in a Tiny_State
// used across multiple threads, you have to ensure it is
// synchronized appropriately.
typedef struct Tiny_Context {
    Tiny_AllocFunction alloc;

    void *userdata;
} Tiny_Context;

typedef struct Tiny_Object Tiny_Object;
typedef struct Tiny_State Tiny_State;

struct Tiny_Value;

// Stores properties about a native
// object. This should be statically allocated
// and only one should exist for each type of
// Native value.
typedef struct Tiny_NativeProp {
    const char *name;

    void (*protectFromGC)(void *);
    void (*finalize)(void *);
} Tiny_NativeProp;

typedef enum {
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

typedef struct Tiny_Value {
    union {
        bool boolean;
        int i;
        float f;
        const char *cstr;  // for TINY_VAL_CONST_STRING
        void *addr;        // for TINY_VAL_LIGHT_NATIVE
        Tiny_Object *obj;
    };

    uint8_t type;
} Tiny_Value;

typedef struct Tiny_Frame {
    int pc, fp;
    int nargs;
} Tiny_Frame;

typedef struct Tiny_StateThread {
    // Each thread can maintain its own context
    // so that you can e.g. override allocation
    // on a per-thread basis.
    Tiny_Context ctx;

    // Each thread stores a reference
    // to its state
    const Tiny_State *state;

    // The garbage collection and heap is thread-local
    Tiny_Object *gcHead;
    int numObjects;
    int maxNumObjects;

    // Global vars are owned by each thread
    Tiny_Value *globalVars;

    int pc, fp, sp;
    Tiny_Value retVal;

    Tiny_Value stack[TINY_THREAD_STACK_SIZE];

    int fc;
    Tiny_Frame frames[TINY_THREAD_MAX_CALL_DEPTH];

    // These keep track of what file/line of source code
    // the instruction at the current PC originated from
    const char *fileName;
    int lineNumber;

    // Userdata pointer. Set to NULL when InitThread is called. Use it for
    // whatever you want
    void *userdata;
} Tiny_StateThread;

typedef Tiny_Value (*Tiny_ForeignFunction)(Tiny_StateThread *thread, const Tiny_Value *args,
                                           int count);

#define TINY_FOREIGN_FUNCTION(name) \
    Tiny_Value name(Tiny_StateThread *thread, const Tiny_Value *args, int count)

extern const Tiny_Value Tiny_Null;

void Tiny_ProtectFromGC(Tiny_Value value);

Tiny_Value Tiny_NewBool(bool value);
Tiny_Value Tiny_NewInt(int i);
Tiny_Value Tiny_NewFloat(float f);
Tiny_Value Tiny_NewConstString(const char *string);
Tiny_Value Tiny_NewLightNative(void *ptr);

// This assumes the given char* was allocated using Tiny_AllocUsingContext or equivalent.
// It takes ownership of the char*, avoiding any intermediate copies.
//
// Note that this does not null terminate the provided string, so if you have C functions
// which rely on null-terminated strings, ensure that you null terminate these yourself.
Tiny_Value Tiny_NewString(Tiny_StateThread *thread, char *str, size_t len);

// This is equivalent to Tiny_NewStringAssumeNullTerminated but it figures out the length assuming
// the given pointer is null-terminated.
Tiny_Value Tiny_NewStringNullTerminated(Tiny_StateThread *thread, char *str);

// Same as Tiny_NewString but it allocates memory for and copies the given string.
//
// Note that this is internally optimized to ensure there is only one allocation for
// both the Tiny object "metadata" and the string itself. If you haven't already
// allocated memory for your string and are ready to hand it off, I highly recommend
// using this instead of Tiny_NewString.
Tiny_Value Tiny_NewStringCopy(Tiny_StateThread *thread, const char *src, size_t len);

// Same as Tiny_NewStringCopy but assumes the given string is null terminated.
Tiny_Value Tiny_NewStringCopyNullTerminated(Tiny_StateThread *thread, const char *src);

Tiny_Value Tiny_NewNative(Tiny_StateThread *thread, void *ptr, const Tiny_NativeProp *prop);

static inline bool Tiny_IsNull(const Tiny_Value value) { return value.type == TINY_VAL_NULL; }

static inline bool Tiny_ToBool(const Tiny_Value value) {
    if (value.type != TINY_VAL_BOOL) return false;
    return value.boolean;
}

static inline int Tiny_ToInt(const Tiny_Value value) {
    if (value.type != TINY_VAL_INT) return 0;
    return value.i;
}

static inline float Tiny_ToFloat(const Tiny_Value value) {
    if (value.type != TINY_VAL_FLOAT) return 0;
    return value.f;
}

static inline float Tiny_ToNumber(const Tiny_Value value) {
    if (value.type == TINY_VAL_FLOAT) return value.f;
    if (value.type != TINY_VAL_INT) return 0;

    return (float)value.i;
}

// Returns NULL if the value isn't a string/const string
const char *Tiny_ToString(const Tiny_Value value);

// Returns 0 if the value isn't a string/const string
size_t Tiny_StringLen(const Tiny_Value value);

// Returns value.addr if its a LIGHT_NATIVE
// Returns the normal native address otherwise
void *Tiny_ToAddr(const Tiny_Value value);

// This returns NULL if the value is a LIGHT_NATIVE instead of a NATIVE
// It would also return NULL if the NativeProp supplied when the object was
// created was NULL, either way, you have no information, so deal with it.
const Tiny_NativeProp *Tiny_GetProp(const Tiny_Value value);

// Returns Tiny_Null if value isn't a struct.
// Asserts if index is out of bounds
Tiny_Value Tiny_GetField(const Tiny_Value value, int index);

Tiny_State *Tiny_CreateState(void);

// Note how the Context is copied.
Tiny_State *Tiny_CreateStateWithContext(Tiny_Context ctx);

// Exposes an opaque type of the given name.
// The same typename can be registered multiple times, but it will only be
// defined once.
//
// This can be used to create fully statically typed APIs from your C code.
//
// For example:
//
// Tiny_RegisterType(state, "DBConn");
// Tiny_RegisterType(state, "DBCursor");
//
// Tiny_BindFunction(state, "db_conn_create(str): DBConn", DBConnCreate);
// Tiny_BindFunction(state, "db_query(DBConn, str): DBCursor", DBQuery);
// Tiny_BindFunction(state, "db_conn_delete(DBConn): void", DBConnDelete);
void Tiny_RegisterType(Tiny_State *state, const char *name);

// Exposes the provided C function as a callable function in
// Tiny code.
//
// You can declare a Tiny_ForeignFunction by using the TINY_FOREIGN_FUNCTION
// macro above (or explicitly typing out the prototype).
//
// The sig string provides the signature of the function in tiny
// code. The syntax of the signature is similar to the equivalent
// declaration in Tiny, except parameter names are omitted.
//
// For example, if I had a foreign function which added two
// integers, I could expose it as follows:
//
// Tiny_BindFunction(state, "add(int, int): int", Add);
//
// You can also use ... to indicate a varargs function:
//
// Tiny_BindFunction(state, "myprint(str, ...): void", MyPrint);
//
// Lastly, you can omit all the types if you want this to be
// an untyped function:
//
// Tiny_BindFunction(state, "myprint", MyPrint);
void Tiny_BindFunction(Tiny_State *state, const char *sig, Tiny_ForeignFunction func);

void Tiny_BindConstBool(Tiny_State *state, const char *name, bool b);
void Tiny_BindConstInt(Tiny_State *state, const char *name, int i);
void Tiny_BindConstFloat(Tiny_State *state, const char *name, float f);
void Tiny_BindConstString(Tiny_State *state, const char *name, const char *value);

void Tiny_CompileString(Tiny_State *state, const char *name, const char *string);
void Tiny_CompileFile(Tiny_State *state, const char *filename);

void Tiny_DeleteState(Tiny_State *state);

void Tiny_InitThread(Tiny_StateThread *thread, const Tiny_State *state);
void Tiny_InitThreadWithContext(Tiny_StateThread *thread, const Tiny_State *state,
                                Tiny_Context ctx);

// Sets the PC of the thread to the entry point of the program
// and allocates space for global variables if they're not already
// allocated
// Requires that state is compiled
void Tiny_StartThread(Tiny_StateThread *thread);

// Returns -1 if the global doesn't exist
// Do note that this will return -1 for global constants as well (those are
// inlined wherever they are used, so they don't really exist)
int Tiny_GetGlobalIndex(const Tiny_State *state, const char *name);

// Returns -1 if the function doesn't exist
int Tiny_GetFunctionIndex(const Tiny_State *state, const char *name);

// Returns the thread->globalVars[globalIndex] (asserts if index < 0)
// You must have started the thread or called Tiny_CallFunction for this to work
// because otherwise, the thread's global variables might not be allocated.
// Don't worry, this will assert that they have.
Tiny_Value Tiny_GetGlobal(const Tiny_StateThread *thread, int globalIndex);

// Sets a global variable at the given index to the given value (asserts if
// index < 0) You must have started the thread or called Tiny_CallFunction for
// this to work because otherwise, the thread's global variables might not be
// allocated. Don't worry, this will assert that they have.
void Tiny_SetGlobal(Tiny_StateThread *thread, int globalIndex, Tiny_Value value);

// Runs the thread until the function exits and returns the retVal.
// functionIndex can be retrieved using Tiny_GetFunctionIndex.
// The only requirement is that the thread has been initialized.
// You can even call this from a foreign function. It keeps track of the
// state of the thread prior to the function call and restores it afterwards.
// This also allocates globals if the thread hasn't been started already, and in
// that case, once the function call is over, the thread will be "done".
Tiny_Value Tiny_CallFunction(Tiny_StateThread *thread, int functionIndex, const Tiny_Value *args,
                             int count);

static inline bool Tiny_IsThreadDone(const Tiny_StateThread *thread) { return thread->pc < 0; }

// Run a single cycle of the thread.
// Could potentially trigger garbage collection
// at the end of the cycle.
// Returns whether the cycle was executed or not.
bool Tiny_ExecuteCycle(Tiny_StateThread *thread);

// Uses the allocator provided to the StateThread to allocate/free memory.
// This should be used instead of global malloc to ensure you play nice with
// all the situations in which Tiny is used.
//
// See Tiny_AllocFunction above for how this can be used.
static inline void *Tiny_AllocUsingContext(Tiny_StateThread *thread, void *ptr, size_t size) {
    return thread->ctx.alloc(ptr, size, thread->ctx.userdata);
}

// Gives access to fast dynamically allocated array type.
// Requires std.c and array.h/array.c
void Tiny_BindStandardArray(Tiny_State *state);

// Gives access to fast dictionary type.
// Requires std.c and dict.h/dict.c
void Tiny_BindStandardDict(Tiny_State *state);

// Gives access to a variety of IO functions.
// Requires std.c
void Tiny_BindStandardIO(Tiny_State *state);

// Provides general functions ala stdlib.h
// Requires std.c
void Tiny_BindStandardLib(Tiny_State *state);

// Provides an i64 type for working with 64-bit integers.
// See std.c for details.
void Tiny_BindI64(Tiny_State *state);

void Tiny_DestroyThread(Tiny_StateThread *thread);

// The following functions are part of the "advanced" API for Tiny.
// They allow you to access certain aspects of the compiler (in this
// case, the symbol table) in order to improve your bindings.
//
// You shouldn't need these in most cases, but if you do, please refer
// to std.c for examples of how you can take advantage of them.

typedef enum Tiny_ModuleResultType {
    TINY_MODULE_SUCCESS = 0,
    TINY_MODULE_ERROR = 1
} Tiny_ModuleResultType;

typedef struct Tiny_ModuleResult {
    Tiny_ModuleResultType type;
    const char *errorMessage;
} Tiny_ModuleResult;

// This function is called for each instance of the `use` statement in Tiny code.
//
// It runs right after we've parsed all the code but just
// before we type check all the code and check for calls to
// undefined functions/references to undefined structs.
//
// What this means is that your module can bind functions/types on the fly based on the
// provided arguments and it can also reference all the existing types in the code to do so.
//
// This is very useful for making generic modules (see the array module in std.c for example)
// or generating serializers/deserializers for user-defined types by using Tiny_CompileString
// within these functions.
typedef Tiny_ModuleResult (*Tiny_ModuleFunction)(Tiny_State *state, char *const *args, int nargs,
                                                 const char *asName);

// This is the primary struct for the Tiny symbol table.
// It is exposed so that bindings can offer reflection capabilities
// and improve type safety.
typedef enum {
    TINY_SYM_GLOBAL,
    TINY_SYM_LOCAL,
    TINY_SYM_CONST,
    TINY_SYM_FUNCTION,
    TINY_SYM_FOREIGN_FUNCTION,
    TINY_SYM_FIELD,
    TINY_SYM_MODULE,

    TINY_SYM_TAG_VOID,
    TINY_SYM_TAG_BOOL,
    TINY_SYM_TAG_INT,
    TINY_SYM_TAG_FLOAT,
    TINY_SYM_TAG_STR,
    TINY_SYM_TAG_ANY,
    TINY_SYM_TAG_FOREIGN,
    TINY_SYM_TAG_STRUCT
} Tiny_SymbolType;

typedef int Tiny_TokenPos;

// For all the fields marked with `// array` below, you can use
// the Tiny_SymbolArrayCount() function to get the length of the arrays.
typedef struct Tiny_Symbol {
    Tiny_SymbolType type;
    char *name;

    Tiny_TokenPos pos;

    union {
        struct {
            bool initialized;  // Has the variable been assigned to?
            bool scopeEnded;   // If true, then this variable cannot be accessed anymore
            int scope, index;

            struct Tiny_Symbol *tag;
        } var;  // Used for both local and global

        struct {
            struct Tiny_Symbol *tag;

            union {
                bool bValue;  // for bool
                int iValue;   // for char/int
                int fValue;   // for float
                int sIndex;   // for string
            };
        } constant;

        struct {
            int index;

            struct Tiny_Symbol **args;    // array
            struct Tiny_Symbol **locals;  // array

            struct Tiny_Symbol *returnTag;
        } func;

        struct {
            int index;

            // nargs = sb_count
            struct Tiny_Symbol **argTags;  // array
            bool varargs;

            struct Tiny_Symbol *returnTag;

            Tiny_ForeignFunction callee;
        } foreignFunc;

        struct {
            // If a struct type is referred to before definition
            // it is declared automatically but with this field
            // set to false. The compiler will check that no
            // such symbols exist before it finishes compilation.
            bool defined;

            struct Tiny_Symbol **fields;  // array
        } sstruct;

        struct Tiny_Symbol *fieldTag;

        Tiny_ModuleFunction modFunc;
    };
} Tiny_Symbol;

void Tiny_BindModule(Tiny_State *state, const char *name, Tiny_ModuleFunction fn);

size_t Tiny_SymbolArrayCount(Tiny_Symbol *const *arr);

const Tiny_Symbol *Tiny_FindTypeSymbol(Tiny_State *state, const char *name);
const Tiny_Symbol *Tiny_FindFuncSymbol(Tiny_State *state, const char *name);

// TODO(Apaar): Tiny_FindFuncSymbol, Tiny_FindConstSymbol, etc

#endif

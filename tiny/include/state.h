#pragma once

// SOMETIME IN THE SUMMER 2019:
// We will have an "import" statement that will allow one to
// import other tiny files. Those files get loaded and compiled
// as separate modules. We will have a ModuleCache structure
// provided by the user to the compiler to store these modules,
// and the user can then reference them by name from the ModuleCache.
// Then they can run a module in the VM, which will run the modules
// they depend on. So when you run, you must provide the modulecache
// because the VM will look up the modules (by some index I guess)
// and then execute their code recursively,

// Holidays 2019:
// We don't want the user to have to worry about modules from the C side that much. 
// A state will retain all modules and code. The VM doesn't even need to worry about
// the existence of modules this way. Anytime you compile some code, we simply append
// it to the end of the existing code and keep track of a module symbol which can
// be used to look up symbols in the module. I suppose each module can maintain its
// own symbol table; the parser won't own it. Imported modules will 

typedef struct Tiny_State Tiny_State;

typedef struct Tiny_LocalRoots
{
    // Buffer
    // Offset from frame pointer
    int8_t* indices;
} Tiny_LocalRoots;

Tiny_State* Tiny_CreateState(Tiny_Context* ctx);

void Tiny_CompileString(Tiny_State* state, const char* moduleName, const char* str, size_t length);

void Tiny_DeleteState(Tiny_State* state);

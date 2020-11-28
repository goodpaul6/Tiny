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
// own symbol table; the parser won't own it.

// November 2020:
// I'm lookin at the module ideas above with fresh eyes and wondering why we don't
// isolate the modules at the frontend level completely? Couldn't we have a parser
// per module, and any other modules it imports are resolved after the successful
// parsing of every module? This way, a module that fails to parse (or even typecheck
// in a later stage) doesn't affect ones that do (other than those that depend
// on the module).
//
// As for the codegen, I think the idea of emitting into a single contiguous code
// stream is fine.
//
// So the idea would be:
// - Have a map of modules (name -> parser maybe)
// - Given some source code, parse it completely
// - Parse all of its module dependencies
// -- Let the user provide the module resolution function (load relative file is sane default)
// -- Ignore modules that have already been parsed
// - If everything parses successfully, run the resolution phase
// -- For inter-module dependencies, we must be able to examine symbol tables across parsers
// -- Could store a "module" symbol which stores a reference to our Module struct
// - If everything resolves successfully, start compile the modules
// - If user is in "optimized" mode, we can discard all of the frontend (ASTs, symbols, etc)
// - Run code, be happy

typedef struct Tiny_State Tiny_State;

typedef struct Tiny_LocalRoots {
    // Buffer
    // Offset from frame pointer
    int8_t* indices;
} Tiny_LocalRoots;

Tiny_State* Tiny_CreateState(Tiny_Context* ctx);

void Tiny_CompileString(Tiny_State* state, const char* moduleName, const char* str, size_t length);

void Tiny_DeleteState(Tiny_State* state);

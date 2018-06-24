# Tiny
Tiny is a small embeddable compiler and bytecode interpreter; it is designed to be easy to embed
and does its best to avoid doing allocations/garbage collection.

## Examples
```
// Reverse polish notation calculator
stack := array()
op = ""

while op != "quit" {
    op = input()

    if strchar(op, 0) == '+' { 
        array_push(stack, array_pop(stack) + array_pop(stack))
    } else if strchar(op, 0) == '-' {
        array_push(stack, array_pop(stack) - array_pop(stack))
    } else if strchar(op, 0) == '*' {
        array_push(stack, array_pop(stack) * array_pop(stack))
    } else if strchar(op, 0) == '/' {
        array_push(stack, array_pop(stack) / array_pop(stack))
    } else if op == "=" {
        print(array_pop(stack))
    } else if op != "quit" {
        array_push(stack, ston(op))
    }
}
```

## Usage
You can use CMake to build a static library which you can integrate into your project
like any other. Alternatively, you can just copy `include/tiny.h`, `include/tiny_detail.h` and `src/tiny.c` (also `src/tinystd.c` if you want the standard library) into your project.

## Embedding
The entire API available to the host application is supplied in `include/tiny.h`.
Here is few examples of how you could use Tiny in your program.

### Game Example

![Alt text](examples/game/images/game.gif?raw=true "Tiny Game")

I've written a small game which is scripted using Tiny. This example makes no use of garbage collection; in fact, there is no dynamic allocation being done in the scripts at all. 
You can find the code in the `examples/game` subdirectory of the repository. I made use of https://bitbucket.org/rmitton/tigr to facilitate the windowing, graphics and input.

Notice how every entity in the game has a `Tiny_StateThread` encapsulating its execution state. Since `Tiny_StateThread` is relatively lightweight, you can have hundreds, even thousands of them.

### Text Editor Example
Let's say I've written a text editor in C and I want users to be able to write plugins
for it, but I don't want them to have to write/compile C code in order to do it. This is
a good use case for something like a scripting language.

```c99
// The plugin subsystem for an imaginary text editor
#include "tiny.h"

#define MAX_PLUGINS 32

typedef struct
{
    // ...
    int numPlugins;
    Tiny_State* plugins[MAX_PLUGINS];
} Editor;

static Tiny_Value ReplaceAll(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    // We store the Editor* in the thread's userdata field
    Editor* ed = thread->userdata;

    // Make sure that 2 arguments are supplied
    if(count != 2) {
        PluginError(ed, "Invalid number of arguments supplied to replace_all.");
        return Tiny_Null;
    }

    const char* from = Tiny_ToString(args[1]);
    const char* to = Tiny_ToString(args[2]);

    // Make sure these are valid strings
    if(!from || !to) {
        PluginError(ed, "Invalid strings supplied to replace_all.");
        return Tiny_Null;
    }

    int numReplaced = EditorReplaceAll(ed, from, to);

    return Tiny_NewNumber((double)numReplaced);
}

void LoadPlugin(Editor* editor, const char* filename)
{
    assert(editor->numPlugins < MAX_PLUGINS);

    Tiny_State* state = Tiny_CreateState();

    Tiny_BindFunction(state, "replace_all", ReplaceAll);

    // If this were an actual editor I'd supply more functions

    Tiny_CompileFile(state, filename);

    editor->plugins[editor->numPlugins++] = state;
}

void DestroyPlugins(Editor* editor)
{
    for(int i = 0; i < editor->numPlugins; ++i) {
        Tiny_DeleteState(editor->plugins[i]);
    }
}

// We can call this if we want to run the entire script all at once
void RunPlugin(Editor* editor, int pluginIndex)
{
    assert(pluginIndex >= 0 && pluginIndex < editor->numPlugins);

    // Let's spin up a single "thread" to run this plugin
    Tiny_StateThread thread;

    Tiny_InitThread(&thread, editor->plugins[pluginIndex]);

    Tiny_StartThread(&thread);

    // Just keep running until the VM halts (i.e. script is done)
    while(Tiny_ExecuteCycle(&thread));

    Tiny_DestroyThread(&thread);
}

// We can call this if we want to spin up a thread and execute cycles
// whenever we can (i.e. run the plugin asynchronously).
Tiny_StateThread* StartPlugin(Editor* editor, int pluginIndex)
{ 
    assert(pluginIndex >= 0 && pluginIndex < editor->numPlugins);

    // Just like for RunPlugin
    Tiny_StateThread* thread = malloc(sizeof(Tiny_StateThread));

    Tiny_InitThread(thread, editor->plugins[pluginIndex]);

    // We start the thread, but we don't execute any cycles
    // the user is responsible for doing that.
    Tiny_StartThread(thread);

    return thread;
}
```

And that's an example of how one might integrate Tiny into their application.

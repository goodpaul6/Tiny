# Tiny
Tiny is a small statically-typed language with an embeddable compiler and bytecode interpreter; it is designed to be easy to embed
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
like any other. Alternatively, you can just copy the few files inside `include` and `src` into your project.

## Embedding
The entire API available to the host application is supplied in `include/tiny.h`.
Here is few examples of how you could use Tiny in your program.

### Game Example

![Alt text](examples/game/images/game.gif?raw=true "Tiny Game")

I've written a small game which is scripted using Tiny. This example makes no use of garbage collection; in fact, there is no dynamic allocation being done in the scripts at all. 
You can find the code in the `examples/game` subdirectory of the repository. I made use of https://bitbucket.org/rmitton/tigr to facilitate the windowing, graphics and input.

Notice how every entity in the game has a `Tiny_StateThread` encapsulating its execution state. Since `Tiny_StateThread` is relatively lightweight, you can have hundreds, even thousands of them.

### Text Editor Example
![Alt text](examples/notepad/images/display.gif?raw=true "Tiny Notepad")

I got carried away and wrote a vim-like text editor using this language. 
I wrote all the buffer manipulation and graphics code in C and then exposed an interface for the editor logic. Have a look at `examples/notepad`.


# Tiny

<img src="https://github.com/goodpaul6/Tiny/assets/854222/a5ebffeb-782e-41fc-b1bb-c50d5dbcb447" width="250" />


Tiny is a small statically-typed language with an embeddable compiler and bytecode interpreter; it is designed to be easy to embed
and does its best to avoid doing allocations/garbage collection.

```
// Reverse polish notation calculator

stack := array()

// The type of the 'stack' variable is inferred to be 'array'
// I can also explicitly specify it by doing
// stack: array = array(), but that's no fun

op := ""

// Constants
plus_op     :: '+'
minus_op    :: '-'
mul_op      :: '*'
div_op      :: '/'
print_op    :: "="
quit_op     :: "quit"

while op != quit_op {
    op = input()

    if stridx(op, 0) == plus_op { 
        array_push(stack, array_pop(stack) + array_pop(stack))
    } else if stridx(op, 0) == minus_op {
        array_push(stack, array_pop(stack) - array_pop(stack))
    } else if stridx(op, 0) == mul_op {
        array_push(stack, array_pop(stack) * array_pop(stack))
    } else if stridx(op, 0) == div_op {
        array_push(stack, array_pop(stack) / array_pop(stack))
    } else if op == print_op {
        print(array_pop(stack))
    } else if op != quit_op {
        array_push(stack, ston(op))
    }
}
```

## Usage
You can use CMake to build a static library which you can integrate into your project
like any other. Alternatively, you can just copy the few files inside `include` and `src` into your project.

See [Embedding Tiny](https://github.com/goodpaul6/Tiny/wiki/Embedding-Tiny) for a quick tour of how to get started.

## Examples
Here are a few examples of how Tiny can be used.

### Game Example

![Alt text](examples/game/images/game.gif?raw=true "Tiny Game")

I've written a small game which is scripted using Tiny. This example makes no use of garbage collection; in fact, there is no dynamic allocation being done in the scripts at all. 
You can find the code in the `examples/game` subdirectory of the repository. I made use of https://bitbucket.org/rmitton/tigr to facilitate the windowing, graphics and input.

Notice how every entity in the game has a `Tiny_StateThread` encapsulating its execution state. Since `Tiny_StateThread` is relatively lightweight, you can have hundreds, even thousands of them.

### Text Editor Example
![Alt text](examples/notepad/images/display.gif?raw=true "Tiny Notepad")

I got carried away and wrote a vim-like text editor using this language. 
I wrote all the buffer manipulation and graphics code in C and then exposed an interface for the editor logic. Have a look at `examples/notepad`.

### Multithreaded Web Server Example
I created a webserver which is capable of handling a large amount of concurrent connections with a variety of web application
development utilities (async processing, routing, templating). Have a look at `examples/server`. I followed golang's 
"Writing Web Applications" tutorial which guides users through making a wiki and replicated that in tiny.

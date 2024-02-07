# Tiny

<p align="center">
    <img src="https://github.com/goodpaul6/Tiny/assets/3721423/99231ec9-c0e6-42da-9c89-3c41128cba8c" width="250" />
</p>

Tiny is a small statically-typed language with an embeddable compiler and bytecode interpreter; it is designed to be easy to embed
and does its best to avoid doing allocations/garbage collection.

<p align="center">
    <img src="https://github.com/goodpaul6/Tiny/assets/3721423/990cb99b-1a46-4601-b3a6-fe9cc50ba880" alt="RPN Calculator Code" width="800" />
</p>

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

![Alt text](examples/server/images/wiki.gif?raw=true "Tiny Wiki")

I created a webserver which is capable of handling a large amount of concurrent connections with a variety of web application
development utilities (async processing, routing, templating). Have a look at `examples/server`. I followed golang's
"Writing Web Applications" tutorial which guides users through making a wiki and replicated that in tiny.

### Advent of Code

I used Tiny to [solve Day 19 of Advent of Code 2023](https://github.com/goodpaul6/advent/blob/master/2023/day19.tiny). It went surprisingly
well. I did some other days as well; you can check out the `advent` repo to find out.

You can run the solution by building `terp` in the examples and then running `tiny_terp day19.tiny` and putting the input in `inputs/day19.txt`
in your working directory.
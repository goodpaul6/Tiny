# TODO

* TEST Short circuting &&
* TEST Short circuiting ||
* TEST Break and continue in for loops
* TEST Got rid of null-terminated strings but didn't really add too many tests

* BAD Accessing null values causes segfault
* BAD No type aliases (strong)

* BUG The following compiles without error (no checking of whether function returns value)
```
func test(): int {}
```

* BAD No error types/error info
    * Add "error" primitive type which is just an error code (int? string? it's own primitive type)
    and a pointer to a context object
* BAD No runtime type information on struct object (could probably stuff a uint16_t struct ID somewhere at least?)
    * Could even just get rid of teh `nfields` since I think that's only used for pretty printing
    and debug checks
* BAD Cannot access type information in C binding functions
    * Would be mega useful to make things type-safe
    * For example, could allow you to succinctly define a `delegate` binding
        * Just need the function name, nothing else
* BAD No interface to allocate/deallocate using the thread's context allocator
* BAD No standard regex
* BAD No syntax highlighting
* BAD Pretty printing `%q` in printf
* BAD No panics??
* BAD Memory unsafety introduced by varargs functions (unless we do runtime type checking)
    * For example, `i64_add_many`
* BAD Sometimes error messages point to the wrong line (off by one?)
* BAD No functions-as-values (not even without captures)
    * Could patch this hole with runtime polymorphism but ehhhhh
    * Could also technically implement this with a C library haha
* BAD No ranges/range-based loops
* BAD All reference types are nullable?
* BAD No multiline comments
* BAD No builtin array or dict
    * Mainly for type safety; parametric polymorphism (at the library level only?) could solve this
    * The library-only parapoly prevents the script code from becoming too complex
    * Builtin array or dict will probably cover most use cases though (see Golang)
* BAD No 64-bit integers
* BAD No named struct initializer
* BAD No char type
* BAD No polymorphism of any kind

* Make `CurTok` local or at least thread local

* Pass in alignment to used provided allocation function

* Refactor VM from compiler

* First class types (store a "type" which is just an integer in the VM)

* Once "any" is safe, we can have typed bytecode instructions; `OP_ADD_INT`, `OP_EQUAL_STRING` etc
    * Untagged values

* Function overloading

* "Method" sugar (first argument to func matches type of x => x.func())

* Do not `exit` anywhere in the library; user code must be able to handle errors

* Less haphazard allocation: Allow the user to supply a malloc, use Arenas

* Function return type inference (Kind of unsafe at times tho)

* Interfaces? There is currently no ways of doing polymorphism and I think the golang approach
  to interfaces is pretty nice:

```
interface Stringable
{
    to_string(): str
}

struct Vec2
{
    x: float
    y: float
}

func to_string(v: Vec2): str {
    return strcat(ntos(v.x), ",", ntos(v.y))
}

func stringify(s: Stringable): str {
    return s.to_string()
}

func thing(v: Vec2) {
    // This still works because there might be functionality that was created to work with certain
    // interfaces that you don't want to repeat for your particular struct
    v.stringify()
}

```

* Anonymous functions (closures)

HARD TO IMPLEMENT AND MAYBE OVERSTEPPING THE BOUNDARIES OF A SCRIPTING LANGUAGE:
* SIMPLE GENERICS (See how C# does it):

```
// NOTE: This is not a C++ template; the types are just used for type-checking at the call site;
// the use of a generic type inside the body of the function is simply not allowed (i.e. trying to
// access a property on an object of that type for example)
// SYNTAX FOR COMPILE-TIME PARAMETER IS NOT FINAL
func get(e: entity, $t): t { ... }

// e is an entity; this will produce an ItemComponent; cast is not necessary
e.get(ItemComponent)
```

Of course, if it's implemented at the tiny level, then generics must also be implemented at the C level:
```c
// $$t means pass in the actual type value as well (so we can inspect it in the C code to get the appropriate component for example)
Tiny_BindFunction(state, "get(entity, $$t): t", Lib_GetComponent);
```

```
Tiny_RegisterType(state, "array($t)");

// $t is matched with the t of the array passed in
Tiny_BindFunction(state, "get(array($t), int): t");
```

# Done

* BUG Assigning to arguments doesn't seem to work, repro


```
func mutate_arg(i: int) {
    i = 10
    // Prints 5
    printf("%i\n", i)
}

mutate_arg(5)
```

* BAD NULL TERMINATED STRINGS?? 
* BUG The following compiles without error
```
func split_lines(s: str): array {
    return ""
}
```

* BAD || does not short circuit
* BAD %c is not handled as a format specifier
* BUG `&&` doesn't short circuit?
* BUG `continue` doesn't seem to run the "step" part of the for loop
* Make sure VM bytecode instructions and data are aligned properly
* Safer "any" type: must be explicitly converted
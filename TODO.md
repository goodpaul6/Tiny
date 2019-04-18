* Make sure VM bytecode instructions and data are aligned properly (DONE)

* First class types (store a "type" which is just an integer in the VM)
* Safer "any" type: must be explicitly converted

* Once "any" is safe, we can have typed bytecode instructions; `OP_ADD_INT`, `OP_EQUAL_STRING` etc

* Function overloading
* "Method" sugar (first argument to func matches type of x => x.func())

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

# TODO
+ Implement functions
    + Parse functions
    + Parse calls
    - Implement interpretation for functions
- Implement subset of virtual machine similar to existing branch snow-common
    - Just enough to get the current interpretation functionality in
- Implement semicolon insertion based on Go spec
- Migrate to using variant for AST
    - Allocate in a pool similar to TypeNamePool
    - Makes removing/tracking allocations easier 
- Implement if statements
- Implement while loops
- Implement for loops
- Implement symbol table
- Implement type checking
- Implement operator precedence
- Implement virtual machine
- Design C interface
- Allow users to use pass in a custom allocator for the library

package main

// Literals
Ast_Literal :: union {bool, i64, f64, rune, string}

// Any identifier
Ast_Ident :: distinct string

// Any AST that has one child (so '-x' but also '(x)')
Ast_Unary :: struct {
    op: rune,
    rhs: ^Ast_Node,
}

// Anything that has two children (so 'x * x' but also x[x] and x.y)
Ast_Binary :: struct {
    op: rune,
    lhs: ^Ast_Node,
    rhs: ^Ast_Node,
}

// Just a container for sub-nodes, use `first_child` to get the head
Ast_Block :: struct {}

// This handles if, for, and while all-in-one
Ast_Control_Flow :: struct {
    // Optional initializer statement.
    init: ^Ast_Node,

    cond: ^Ast_Node,
    body: ^Ast_Node,

    // If this is true, then we loop back to the condition after
    // executing body
    should_loop: bool,

    // Optional. If this is specified on a loop, then it gets triggered
    // if the loop condition fails on the initial check.
    else_body: ^Ast_Node,
}

// Special case loop (foreach)
Ast_Range_Loop :: struct {
    elem_name: string,

    // if empty then there is none specified (its anonymous)
    index_name: string,

    // The object being traversed
    range: ^Ast_Node,
}

Ast_Jump :: struct {
    // TODO(Apaar): Add support for multi-level breaks or just gotos
    kind: enum {Break, Continue}
}

// This is not a part of the Ast data structure hence the lack of prefix.
// We still allocate it in the Ast allocator so I'm using a linked list
// regardless.
Aggregate_Decl_Field :: struct {
    next: ^Aggregate_Decl_Field,

    name: string,

    // Technically this is just supposed to be a qualified name, but we use an Ast_Node
    // to store it regardless (x.y is just Ast_Binary with op = '.' and two identifiers)
    type: ^Ast_Node,
}

Ast_Aggregate_Decl :: struct {
    // If it's an object, then it's boxed, otherwise it is not
    is_object: bool,

    first_field: ^Aggregate_Decl_Field,
    last_field: ^Aggregate_Decl_Field,
}

Ast_Node_Sub :: union #no_nil {
    Ast_Literal, 
    Ast_Ident,
    Ast_Unary, 
    Ast_Binary, 
    Ast_Block,
    Ast_Control_Flow,
    Ast_Range_Loop,
    Ast_Jump,
    Ast_Aggregate_Decl,
}

// AST nodes basically just form an n-ary tree.
// If you wanted to traverse all of the nodes of the tree, you can do so straightforwardly
// just by traversing the children via `first_child`.
//
// If you need more info about a particular node, you can use the `sub` field.
//
// We defer all the symbol table stuff to a separate pass, although we still cache
// the symbol stuff on this Ast_Node for that phase. The reason I do this is because
// that allows us to do transformation passes over the Ast without worrying about
// symbol table manipulation.
Ast_Node :: struct {
    next: ^Ast_Node,

    first_child: ^Ast_Node,
    last_child: ^Ast_Node,

    pos: Token_Pos,

    sub: Ast_Node_Sub,
}



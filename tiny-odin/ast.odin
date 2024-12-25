package main

Null_Literal_Value :: struct {}

Qual_Name :: struct {
    // Optional, could be empty
    mod_name: string,

    name: string
}

// Literals
Ast_Literal :: union {Null_Literal_Value, bool, i64, f64, rune, string}

// Any identifier
Ast_Ident :: distinct string

// Any AST that has one child (so '-x')
Ast_Unary :: struct {
    op: string,
    rhs: ^Ast_Node,
}

// Anything that has two children (so 'x * x' but also x[x] and x.y)
Ast_Binary :: struct {
    // This is not allocated, it's just a literal, but I guess that
    // doesn't matter since we're using the temp allocator
    op: string,
    lhs: ^Ast_Node,
    rhs: ^Ast_Node,
}

// This handles if, for, and while all-in-one
Ast_Control_Flow :: struct {
    // Optional initializer statement.
    init: ^Ast_Node,

    cond: ^Ast_Node,
    
    // Optional step
    step: ^Ast_Node,

    body: ^Ast_Node,

    // Optional. If this is specified on a loop, then it gets triggered
    // if the loop condition fails on the initial check.
    else_body: ^Ast_Node,

    // If this is true, then we loop back to the condition after
    // executing body
    should_loop: bool,
}

// Special case loop (foreach)
Ast_Range_Loop :: struct {
    elem_name: string,

    // if empty then there is none specified (its anonymous)
    index_name: string,

    // The object being traversed
    range: ^Ast_Node,

    body: ^Ast_Node,
}

Ast_Jump :: struct {
    // TODO(Apaar): Add support for multi-level breaks or just gotos
    kind: enum {Break, Continue}
}

Def_Elem :: struct {
    next: ^Def_Elem,

    name: string,
    type: ^Qual_Name,
}

Def_Kind :: enum{Object, Struct, Func}

Ast_Def :: struct {
    kind: Def_Kind,

    name: string,

    // For functions, these are the arguments.
    // For aggregates (Object, Struct), these are the fields.
    first_elem: ^Def_Elem,
    last_elem: ^Def_Elem,

    // For functions
    return_type: ^Qual_Name,

    // For functions
    body: ^Ast_Node,
}

Ast_Call :: struct {
    callee: ^Ast_Node,

    first_arg: ^Ast_Node,
    last_arg: ^Ast_Node,
}

Ast_Return :: struct {
    value: ^Ast_Node
}

New_Arg :: struct {
    next: ^New_Arg,

    // If this is empty, then it's an unnamed
    // arg. We'll check that the user did not
    // mix named and unnamed args.
    name: string,
    value: ^Ast_Node
}

Ast_New :: struct {
    type: ^Qual_Name,

    first_arg: ^New_Arg,
    last_arg: ^New_Arg,
}

Ast_Node_Sub :: union #no_nil {
    Ast_Literal, 
    Ast_Ident,
    Ast_Unary, 
    Ast_Binary, 
    Ast_Control_Flow,
    Ast_Range_Loop,
    Ast_Jump,
    Ast_Def,
    Ast_Call,
    Ast_Return,
    Ast_New,
}

// AST nodes basically just form an n-ary tree.
// There is a single traverse function that will let you go down the tree as needed.
//
// If you need more info about a particular node, you can use the `sub` field.
//
// We defer all the symbol table stuff to a separate pass, although we still cache
// the symbol stuff on this Ast_Node for that phase. The reason I do this is because
// that allows us to do transformation passes over the Ast without worrying about
// symbol table manipulation.
Ast_Node :: struct {
    next: ^Ast_Node,

    pos: Token_Pos,

    sub: Ast_Node_Sub,
}

ast_traverse :: proc(root: ^Ast_Node, ctx: ^$T, fn: proc(node: ^Ast_Node, ctx: ^T) -> bool) -> bool {
    if root == nil {
        return true
    }

    if !fn(root, ctx) {
        return false
    }

    switch sub in root.sub {
        case .Ast_Literal: return true
        case .Ast_Ident: return true

        case .Ast_Unary: 
            return ast_traverse(sub.rhs, ctx, fn)

        case .Ast_Binary:
            return ast_traverse(sub.lhs, ctx, fn) && 
                   ast_traverse(sub.rhs, ctx, fn)

        case .Ast_Control_Flow:
            return ast_traverse(sub.init, ctx, fn) &&
                   ast_traverse(sub.cond, ctx, fn) &&
                   ast_traverse(sub.body, ctx, fn) &&
                   ast_traverse(sub.else_body, ctx, fn)

        case .Ast_Range_Loop:
            return ast_traverse(sub.range, ctx, fn)

        case .Ast_Def:
            return ast_traverse(sub.body, ctx, fn)

        case .Ast_Call:
            for node := sub.first_arg; node != nil; node = node.next {
                if !ast_traverse(node, ctx, fn) {
                    return false
                }
            }

            return true

        case .Ast_Return:
            return ast_traverse(sub.value, ctx, fn)

        case .Ast_New:
            return true
    }

    return true
}

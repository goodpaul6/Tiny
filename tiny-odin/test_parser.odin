package main

import "core:testing"
import "core:log"
import "core:fmt"
import "core:encoding/json"

@(test)
test_parse_literal :: proc(t: ^testing.T) {
    p := parser_make("test", `true false 10 0xff 0.35 "hello" 'w' (x)`)
    parser_next_token(&p)
    
    value, err := parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 0,
        sub = Ast_Literal(true)
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 5,
        sub = Ast_Literal(false)
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 11,
        sub = Ast_Literal(i64(10))
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 14,
        sub = Ast_Literal(i64(0xff))
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 19,
        sub = Ast_Literal(f64(0.35))
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 24,
        sub = Ast_Literal("hello")
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 32,
        sub = Ast_Literal(rune('w'))
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, value^, Ast_Node{
        pos = 37,
        sub = Ast_Ident("x")
    })

    value, err = parser_parse_value(&p)
    testing.expect_value(t, err, Parser_Error{
        pos = 39,
        msg = "Unexpected token: (EOF)"
    })
}

@(test)
test_parse_suffixes :: proc(t: ^testing.T) {
    p := parser_make("test", `x.y(hello, 10)`)
    parser_next_token(&p)

    value, err := parser_parse_expr(&p)

    call := value.sub.(Ast_Call)
    // So that it doesn't print pointers
    call.first_arg.next = nil
    s := fmt.tprint(call.callee.sub, call.first_arg, call.last_arg)

    testing.expect_value(t, s, `Ast_Binary{op = ., lhs = &Ast_Node{next = <nil>, pos = 0, sub = "x"}, rhs = &Ast_Node{next = <nil>, pos = 2, sub = "y"}} &Ast_Node{next = <nil>, pos = 4, sub = "hello"} &Ast_Node{next = <nil>, pos = 11, sub = 10}`)
}

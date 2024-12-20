package main

import "core:testing"

@(test)
test_parse_bool :: proc(t: ^testing.T) {
    p := parser_make("test", `true false 10 0xff 0.35 "hello" 'w'`)
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
    testing.expect_value(t, err, Parser_Error{
        pos = 35,
        msg = "Unexpected token: (EOF)"
    })
}



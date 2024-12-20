package main

import "core:testing"

@(test)
test_parse_bool :: proc(t: ^testing.T) {
    p := parser_make("test", `true false 10 0xff 0.35 "hello" 'w'`)
    parser_next_token(&p)
    
    atom, err := parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 0,
        sub = Ast_Literal(true)
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 5,
        sub = Ast_Literal(false)
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 11,
        sub = Ast_Literal(i64(10))
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 14,
        sub = Ast_Literal(i64(0xff))
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 19,
        sub = Ast_Literal(f64(0.35))
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 24,
        sub = Ast_Literal("hello")
    })

    atom, err = parser_parse_atom(&p)
    testing.expect_value(t, atom^, Ast_Node{
        pos = 32,
        sub = Ast_Literal(rune('w'))
    })
}



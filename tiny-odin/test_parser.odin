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
        msg = "Unexpected token (near '(EOF)')"
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

    testing.expect_value(t, s, `Ast_Binary{op = ".", lhs = &Ast_Node{next = <nil>, pos = 0, sub = "x"}, rhs = &Ast_Node{next = <nil>, pos = 2, sub = "y"}} &Ast_Node{next = <nil>, pos = 4, sub = "hello"} &Ast_Node{next = <nil>, pos = 11, sub = 10}`)
}

@(test)
test_parse_binary :: proc(t: ^testing.T) {
    p := parser_make("test", `x * y + 5`)
    parser_next_token(&p)

    value, err := parser_parse_expr(&p)

    sub := value.sub.(Ast_Binary)
    lhs := sub.lhs
    lhs_sub := lhs.sub.(Ast_Binary)

    rhs := sub.rhs

    sub.lhs = nil
    sub.rhs = nil
    
    s := fmt.tprint(sub, lhs_sub, rhs)
    testing.expect_value(t, s, `Ast_Binary{op = "+", lhs = <nil>, rhs = <nil>} Ast_Binary{op = "*", lhs = &Ast_Node{next = <nil>, pos = 0, sub = "x"}, rhs = &Ast_Node{next = <nil>, pos = 4, sub = "y"}} &Ast_Node{next = <nil>, pos = 8, sub = 5}`)
}

@(test)
test_parse_assign :: proc(t: ^testing.T) { 
    p := parser_make("test", `x[0] := 10 * 20`)
    parser_next_token(&p)

    value, err := parser_parse_statement(&p)
    testing.expect_value(t, err, nil)

    sub := value.sub.(Ast_Binary)

    lhs := sub.lhs
    lhs_sub := lhs.sub.(Ast_Binary)

    rhs := sub.rhs
    rhs_sub := rhs.sub.(Ast_Binary)

    sub.lhs = nil
    sub.rhs = nil

    s := fmt.tprint(sub, lhs_sub, rhs_sub)
    testing.expect_value(t, s, `Ast_Binary{op = ":=", lhs = <nil>, rhs = <nil>} Ast_Binary{op = "[", lhs = &Ast_Node{next = <nil>, pos = 0, sub = "x"}, rhs = &Ast_Node{next = <nil>, pos = 2, sub = 0}} Ast_Binary{op = "*", lhs = &Ast_Node{next = <nil>, pos = 8, sub = 10}, rhs = &Ast_Node{next = <nil>, pos = 13, sub = 20}}`)
}

@(test)
test_parse_call :: proc(t: ^testing.T) {
    p := parser_make("test", `x.y(10, 20)`)
    parser_next_token(&p)

    value, err := parser_parse_statement(&p)
    testing.expect_value(t, err, nil)

    sub := value.sub.(Ast_Call)

    sub_callee := sub.callee.sub.(Ast_Binary)
    sub.callee = nil

    sub.first_arg.next = nil

    s := fmt.tprint(sub_callee, sub.first_arg, sub.last_arg)
    testing.expect_value(t, s, `Ast_Binary{op = ".", lhs = &Ast_Node{next = <nil>, pos = 0, sub = "x"}, rhs = &Ast_Node{next = <nil>, pos = 2, sub = "y"}} &Ast_Node{next = <nil>, pos = 4, sub = 10} &Ast_Node{next = <nil>, pos = 8, sub = 20}`)
}

@(test)
test_parse_all :: proc(t: ^testing.T) {
    p := parser_make("test", `
    struct V2 { x: int y: int }
    func add(x: int, y: int): int { return x + y }

    add(10, 20)
    `)
    parser_next_token(&p)

    for p.l.last_tok.kind != .End {
        _, err := parser_parse_statement(&p)
        testing.expect_value(t, err, nil)

        if err, ok := err.(Parser_Error); ok {
            line, col := pos_to_line_col(p.l.src, err.pos)
            log.error(line, col)
            break
        }
    }
}

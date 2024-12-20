package main

import "core:testing"

@(test)
test_lexer :: proc(t: ^testing.T) {
    l := lexer_make("test", `hello world <= '\'' "\"" 0xfeed 1234`)

    t1 := lexer_get_token(&l)
    t2 := lexer_get_token(&l)
    t3 := lexer_get_token(&l)
    t4 := lexer_get_token(&l)
    t5 := lexer_get_token(&l)
    t6 := lexer_get_token(&l)
    t7 := lexer_get_token(&l)
    t8 := lexer_get_token(&l)

    testing.expect_value(t, t1.lexeme, "hello")
    testing.expect_value(t, t2.lexeme, "world")

    testing.expect_value(t, t3.lexeme, "<=")
    testing.expect_value(t, t3.kind, Token_Kind.Other)

    testing.expect_value(t, t4.lexeme, `\'`)
    testing.expect_value(t, t4.kind, Token_Kind.Char)

    testing.expect_value(t, t5.lexeme, `"\""`)
    testing.expect_value(t, t5.kind, Token_Kind.Str)

    testing.expect_value(t, t6.lexeme, `0xfeed`)
    testing.expect_value(t, t6.kind, Token_Kind.Num)

    testing.expect_value(t, t7.lexeme, `1234`)
    testing.expect_value(t, t7.kind, Token_Kind.Num)

    testing.expect_value(t, t8.lexeme, "(EOF)")
    testing.expect_value(t, t8.kind, Token_Kind.End)
}

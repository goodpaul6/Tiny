package main

import "core:fmt"
import "core:strings"
import "core:strconv"

Parser_Error :: struct {
    msg: string,
    // If any, for fontext
    pos: Token_Pos,
}

Parser :: struct {
    l: Lexer,    
}

parser_make :: proc(file_name: string, src: string) -> Parser {
    return {
        l = lexer_make(file_name, src)
    }
}

@(private="file")
next_token :: proc(using p: ^Parser) -> Token {
    return lexer_get_token(&l)
}

parser_next_token :: next_token

@(private="file")
cur_pos_error :: proc(using p: ^Parser, msg: string) -> Parser_Error {
    return {
        msg = msg,
        pos = p.l.last_tok.pos,
    }
}

// Everything goes on the temp allocator
@(private="file")
ast_node_create :: proc(using p: ^Parser, sub: Ast_Node_Sub) -> (node: ^Ast_Node) {
    return new_clone(Ast_Node{
        pos = l.last_tok.pos,
        sub = sub,
    }, context.temp_allocator)
}

@(private="file")
clone_lexeme :: proc(using p: ^Parser) -> string {
    return strings.clone(l.last_tok.lexeme, context.temp_allocator)
}

@(private="file")
parse_num :: proc(using p: ^Parser) -> (ast: Ast_Literal, err: Maybe(Parser_Error)) {
    lexeme := l.last_tok.lexeme

    if i, is_int := strconv.parse_i64(lexeme); is_int {
        ast = i
    } else if f, is_flt := strconv.parse_f64(lexeme); is_flt {
        ast = f
    } else {
        err = cur_pos_error(p, fmt.tprintf("Invalid numeric literal: %s", lexeme))
    }

    return
}

@(private="file")
parse_char :: proc(using p: ^Parser) -> (ast: Ast_Literal, err: Maybe(Parser_Error)) {
    lexeme := l.last_tok.lexeme

    if ch, mb, tail, is_char := strconv.unquote_char(lexeme, '\''); is_char {
        ast = ch
    } else {
        err = cur_pos_error(p, fmt.tprintf("Invalid char literal: %s", lexeme))
    }

    return
}

@(private="file")
parse_str :: proc(using p: ^Parser) -> (ast: Ast_Literal, err: Maybe(Parser_Error)) {
    lexeme := l.last_tok.lexeme

    if s, did_alloc, is_str := strconv.unquote_string(lexeme, context.temp_allocator); is_str {
        ast = did_alloc ? s : strings.clone(s, context.temp_allocator)
    } else {
        err = cur_pos_error(p, fmt.tprintf("Invalid string literal: %s", lexeme))
    }

    return
}

@(private="file")
@(require_results)
expect_token_lexeme :: proc(using p: ^Parser, lexeme: string, msg: string) -> Maybe(Parser_Error) {
    if l.last_tok.lexeme != lexeme {
        return cur_pos_error(p, msg)
    }

    return nil
}

@(private="file")
parse_value :: proc(using p: ^Parser) -> (node: ^Ast_Node, err: Maybe(Parser_Error)) {
    #partial switch l.last_tok.kind {
        case .Error: {
            err = cur_pos_error(p, l.last_tok.lexeme)
            return
        }

        case .Num: {
            lit := parse_num(p) or_return
            node = ast_node_create(p, lit)
        }

        case .Char: {
            lit := parse_char(p) or_return
            node = ast_node_create(p, lit)
        }

        case .Str: {
            lit := parse_str(p) or_return
            node = ast_node_create(p, lit)
        }

        case .Ident: {
            if l.last_tok.lexeme == "true" {
                node = ast_node_create(p, Ast_Literal(true))
            } else if l.last_tok.lexeme == "false" {
                node = ast_node_create(p, Ast_Literal(false))
            } else {
                node = ast_node_create(p, Ast_Ident(clone_lexeme(p)))
            }
        }

        case .Punct: {
            if l.last_tok.lexeme == "(" {
                next_token(p)
                node = parse_expr(p) or_return

                expect_token_lexeme(p, ")", "Expected ')' after previous '('") or_return
                next_token(p)
            }
        }
    }

    if node == nil {
        err = cur_pos_error(p, fmt.tprintf("Unexpected token: %s", l.last_tok.lexeme))
        return
    }

    next_token(p)

    return
}

parser_parse_value :: parse_value

@(private="file")
parse_suffixed_value :: proc(using p: ^Parser) -> (lhs: ^Ast_Node, err: Maybe(Parser_Error)) {
    lhs = parse_value(p) or_return

    for {
        lexeme := l.last_tok.lexeme

        if lexeme == "[" {
            next_token(p)

            lhs = ast_node_create(p, Ast_Binary{
                op = "[",
                lhs = lhs,
                rhs = parse_expr(p) or_return
            })

            expect_token_lexeme(p, "]", "Expected ']' after previous '['") or_return
            next_token(p)

            continue
        } else if lexeme == "." {
            next_token(p)

            pos := l.pos

            rhs := parse_value(p) or_return

            if _, ok := rhs.sub.(Ast_Ident); !ok {
                err = Parser_Error{
                    pos = pos,
                    msg = "Expected identifier after '.'"
                }
                return
            }

            lhs = ast_node_create(p, Ast_Binary{
                op = ".",
                lhs = lhs,
                rhs = rhs,
            })

            continue
        } else if lexeme == "(" {
            next_token(p)

            pos := l.pos

            first_arg: ^Ast_Node
            last_arg: ^Ast_Node

            for l.last_tok.lexeme != ")" {
                node := parse_expr(p) or_return

                if first_arg == nil {
                    first_arg = node
                    last_arg = node
                } else {
                    last_arg.next = node
                    last_arg = node
                }

                if l.last_tok.lexeme == "," {
                    next_token(p)
                } else if l.last_tok.lexeme != ")" {
                    err = Parser_Error{
                        pos = pos,
                        msg = "Expected , or ) in call",
                    }
                    return
                }
            }

            next_token(p)

            lhs = ast_node_create(p, Ast_Call{
                callee = lhs,

                first_arg = first_arg,
                last_arg = last_arg,
            })

            continue
        }

        return
    }
}

@(private="file")
Op_Prec :: struct {
    op: string,
    prec: int
}

@(private="file")
get_prec :: proc(op: string) -> Op_Prec {
    op_to_prec := [?]Op_Prec{
        {"*", 5},
        {"/", 5},
        {"%", 5},
        {"&", 5},
        {"|", 5},

        {"<<", 4},
        {">>", 4},
        {"+", 4},
        {"-", 4},

        {"<=", 3},
        {">=", 3},
        {"==", 3},
        {"!=", 3},
        {"<", 3},
        {">", 3},

        {"&&", 2},
        {"||", 2},
    }

    for op_prec in op_to_prec {
        if op_prec.op == op {
            return op_prec
        }
    }

    return {"", -1}
}

@(private="file")
parse_bin_rhs :: proc(using p: ^Parser, expr_prec: int, lhs: ^Ast_Node) -> (node: ^Ast_Node, err: Maybe(Parser_Error)) {
    lhs := lhs

    for {
        op := get_prec(l.last_tok.lexeme)

        if op.prec < expr_prec {
            node = lhs
            return
        }

        next_token(p)

        rhs := parse_suffixed_value(p) or_return

        next_op := get_prec(l.last_tok.lexeme)

        if next_op.prec > op.prec {
            rhs = parse_bin_rhs(p, op.prec + 1, rhs) or_return
        }

        lhs = ast_node_create(p, Ast_Binary{
            op = op.op,
            lhs = lhs,
            rhs = rhs,
        })
    }
}

@(private="file")
parse_expr :: proc(using p: ^Parser) -> (node: ^Ast_Node, err: Maybe(Parser_Error)) {
    node = parse_suffixed_value(p) or_return
    node = parse_bin_rhs(p, 0, node) or_return

    return
}

parser_parse_expr :: parse_expr

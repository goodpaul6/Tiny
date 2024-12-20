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
ast_node_create :: proc(using p: ^Parser, sub: Ast_Node_Sub) -> ^Ast_Node {
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

        case .Other: {
            if l.last_tok.lexeme == "true" {
                node = ast_node_create(p, Ast_Literal(true))
            } else if l.last_tok.lexeme == "false" {
                node = ast_node_create(p, Ast_Literal(false))
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

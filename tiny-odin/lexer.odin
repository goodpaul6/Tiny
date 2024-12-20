package main

import "core:bytes"

Token_Pos :: int

Token_Kind :: enum { End, Error, Num, Char, Str, Other }

Token :: struct {
    pos: Token_Pos,
    // We keep the kinds pretty high level because the specifics
    // of the token don't really matter to the lexer, and we don't
    // want to unnecessarily complicate the codepaths of the caller.
    //
    // If you just want to use the lexeme, you can.
    kind: Token_Kind,

    // If kind is error, then this is the error message
    lexeme: string,
}

Lexer :: struct {
    file_name: string,
    src: string,

    last: rune,
    pos: Token_Pos,

    last_tok: Token,
}

lexer_make :: proc(file_name: string, src: string) -> Lexer {
    return {
        file_name = file_name,
        src = src,
        last = ' ',
    }
}

@(private="file")
get_char :: proc(l: ^Lexer) -> rune {
    if l.pos >= len(l.src) {
        // One past the end to make our lexeme things work
        l.pos = len(l.src) + 1
        return 0
    }

    ch := l.src[l.pos]
    l.pos += 1

    return rune(ch)
}

@(private="file")
peek :: proc(l: ^Lexer) -> rune {
    return rune(l.src[l.pos])
}

@(private="file")
peek2 :: proc(l: ^Lexer) -> rune {
    if l.pos + 1 >= len(l.src) {
        return 0
    }

    return rune(l.src[l.pos + 1])
}

@(private="file")
is_alpha :: #force_inline proc(ch: rune) -> bool {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z')
}

@(private="file")
is_digit :: #force_inline proc(ch: rune) -> bool {
    return (ch >= '0' && ch <= '9')
}

@(private="file")
is_alnum :: #force_inline proc(ch: rune) -> bool {
    return is_alpha(ch) || is_digit(ch)
}

@(private="file")
is_xdigit :: #force_inline proc(ch: rune) -> bool {
    return is_digit(ch) || 
           (ch >= 'a' && ch <= 'f') || 
           (ch >= 'A' && ch <= 'F')
}

@(private="file")
get_token :: proc(l: ^Lexer) -> Token {
    for bytes.is_space(l.last) {
        l.last = get_char(l)
    }

    start_pos := l.pos - 1

    if l.last == 0 {
        return {start_pos, .End, "(EOF)"}
    }

    if l.last == '/' && peek(l) == '/' {
        for l.last != 0 && l.last != '\n' {
            l.last = get_char(l)
        }

        if l.last != 0 {
            l.last = get_char(l)
        }

        return get_token(l)
    }

    if l.last == '.' && peek(l) == '.' && peek2(l) == '.' {
        l.pos += 2
        l.last = get_char(l)
        return {start_pos, .Other, "..."}
    }

    two_ch := [?]string{
        "&&",
        "||",
        ":=",
        "::",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "|=",
        "&=",
        "==",
        "!=",
        "<=",
        ">=",
        "->",
        "<<",
        ">>",
    }

    for opt in two_ch {
        if l.last == rune(opt[0]) && peek(l) == rune(opt[1]) {
            l.pos += 1
            l.last = get_char(l)

            return {start_pos, .Other, opt}
        }
    }

    one_ch := [?]string {
        "(",
        ")",
        "{",
        "}",
        "[",
        "]",
        "+",
        "-",
        "*",
        "/",
        "%",
        ">",
        "<",
        "=",
        "!",
        "&",
        "|",
        ",",
        ";",
        ":",
        ".",
        "?",
    }

    for opt in one_ch {
        if l.last == rune(opt[0]) {
            l.last = get_char(l)

            return {start_pos, .Other, opt}
        }
    }

    if is_alpha(l.last) || l.last == '_' {
        for is_alnum(l.last) || l.last == '_' {
            l.last = get_char(l)
        }

        lexeme := l.src[start_pos:l.pos-1]
        return {start_pos, .Other, lexeme}
    }

    if is_digit(l.last) {
        for is_digit(l.last) || is_xdigit(l.last) || l.last == 'x' || l.last == '.' {
            l.last = get_char(l)
        }

        lexeme := l.src[start_pos:l.pos-1]
        return {start_pos, .Num, lexeme}
    }

    if l.last == '\'' {
        l.last = get_char(l)

        if l.last == '\\' {
            // The caller can handle the escape sequence
            l.last = get_char(l)
        }

        l.last = get_char(l)
        if l.last != '\'' {
            return {start_pos, .Error, "Expected ' to close previous '."}
        }

        l.last = get_char(l)

        lexeme := l.src[start_pos+1:l.pos-2]

        return {start_pos, .Char, lexeme}
    }

    if l.last == '"' {
        l.last = get_char(l)

        for l.last != 0 && l.last != '"' {
            if l.last == '\\' {
                l.last = get_char(l)
            }

            l.last = get_char(l)
        }

        l.last = get_char(l)

        // We actually include quotes because strconv has a good
        // function to unquote
        lexeme := l.src[start_pos:l.pos-1]

        return {start_pos, .Str, lexeme}
    }

    return {start_pos, .Error, "Unexpected character"}
}

lexer_get_token :: proc(l: ^Lexer) -> Token {
    l.last_tok = get_token(l)
    return l.last_tok
}

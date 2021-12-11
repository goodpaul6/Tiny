#include "lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "stretchy_buffer.h"
#include "util.h"

static void ResetLexeme(Tiny_Lexer *l) {
    if (l->lexeme) stb__sbn(l->lexeme) = 0;
}

static int GetChar(Tiny_Lexer *l) {
    if (l->src[l->pos]) return l->src[l->pos++];
    return 0;
}

inline static int Peek(Tiny_Lexer *l) { return l->src[l->pos]; }

static void ReportError(Tiny_Lexer *l, const char *s, ...) {
    va_list args;

    va_start(args, s);
    Tiny_ReportErrorV(l->fileName, l->src, l->pos, s, args);
    va_end(args);

    exit(1);
}

void Tiny_InitLexer(Tiny_Lexer *l, const char *fileName, const char *src) {
    l->fileName = fileName;
    l->src = src;

    l->lineNumber = 1;
    l->pos = 0;

    l->last = ' ';
    l->lexeme = NULL;
}

Tiny_TokenKind Tiny_GetToken(Tiny_Lexer *l) {
    while (isspace(l->last)) {
        if (l->last == '\n') l->lineNumber++;
        l->last = GetChar(l);
    }

    if (l->last == 0) {
        return TINY_TOK_EOF;
    }

    if (l->last == '/' && Peek(l) == '/') {
        while (l->last && l->last != '\n') {
            l->last = GetChar(l);
        }

        if (l->last) {
            l->last = GetChar(l);
            l->lineNumber += 1;
        }

        return Tiny_GetToken(l);
    }

#define MATCH2(s, tok)                            \
    do {                                          \
        if (l->last == s[0] && Peek(l) == s[1]) { \
            ResetLexeme(l);                       \
            sb_push(l->lexeme, s[0]);             \
            sb_push(l->lexeme, s[1]);             \
            sb_push(l->lexeme, 0);                \
            l->pos += 1;                          \
            l->last = GetChar(l);                 \
            return TINY_TOK_##tok;                \
        }                                         \
    } while (0)

    MATCH2("&&", LOG_AND);
    MATCH2("||", LOG_OR);

    MATCH2(":=", DECLARE);
    MATCH2("::", DECLARECONST);

    MATCH2("+=", PLUSEQUAL);
    MATCH2("-=", MINUSEQUAL);
    MATCH2("*=", STAREQUAL);
    MATCH2("/=", SLASHEQUAL);
    MATCH2("+=", PERCENTEQUAL);
    MATCH2("|=", OREQUAL);
    MATCH2("&=", ANDEQUAL);

    MATCH2("==", EQUALS);
    MATCH2("!=", NOTEQUALS);

    MATCH2("<=", LTE);
    MATCH2(">=", GTE);

#define MATCH(c, tok)              \
    do {                           \
        if (l->last == c) {        \
            ResetLexeme(l);        \
            sb_push(l->lexeme, c); \
            sb_push(l->lexeme, 0); \
            l->last = GetChar(l);  \
            return TINY_TOK_##tok; \
        }                          \
    } while (0)

    MATCH('(', OPENPAREN);
    MATCH(')', CLOSEPAREN);
    MATCH('{', OPENCURLY);
    MATCH('}', CLOSECURLY);
    MATCH('+', PLUS);
    MATCH('-', MINUS);
    MATCH('*', STAR);
    MATCH('/', SLASH);
    MATCH('%', PERCENT);
    MATCH('>', GT);
    MATCH('<', LT);
    MATCH('=', EQUAL);
    MATCH('!', BANG);
    MATCH('&', AND);
    MATCH('|', OR);
    MATCH(',', COMMA);
    MATCH(';', SEMI);
    MATCH(':', COLON);
    MATCH('.', DOT);

    if (isalpha(l->last)) {
        ResetLexeme(l);

        while (isalnum(l->last) || l->last == '_') {
            sb_push(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        sb_push(l->lexeme, 0);

        if (strcmp(l->lexeme, "null") == 0) return TINY_TOK_NULL;

        if (strcmp(l->lexeme, "true") == 0) {
            l->bValue = true;
            return TINY_TOK_BOOL;
        }

        if (strcmp(l->lexeme, "false") == 0) {
            l->bValue = false;
            return TINY_TOK_BOOL;
        }

        if (strcmp(l->lexeme, "if") == 0) return TINY_TOK_IF;
        if (strcmp(l->lexeme, "func") == 0) return TINY_TOK_FUNC;
        if (strcmp(l->lexeme, "foreign") == 0) return TINY_TOK_FOREIGN;
        if (strcmp(l->lexeme, "return") == 0) return TINY_TOK_RETURN;
        if (strcmp(l->lexeme, "while") == 0) return TINY_TOK_WHILE;
        if (strcmp(l->lexeme, "for") == 0) return TINY_TOK_FOR;
        if (strcmp(l->lexeme, "else") == 0) return TINY_TOK_ELSE;
        if (strcmp(l->lexeme, "struct") == 0) return TINY_TOK_STRUCT;
        if (strcmp(l->lexeme, "new") == 0) return TINY_TOK_NEW;
        if (strcmp(l->lexeme, "cast") == 0) return TINY_TOK_CAST;

        return TINY_TOK_IDENT;
    }

    if (isdigit(l->last)) {
        ResetLexeme(l);

        bool isFloat = false;

        while (isdigit(l->last) || (l->last == '.' && !isFloat)) {
            if (l->last == '.') isFloat = true;

            sb_push(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        sb_push(l->lexeme, 0);

        if (isFloat) {
            l->fValue = strtof(l->lexeme, NULL);
        } else {
            l->iValue = strtol(l->lexeme, NULL, 10);
        }

        return isFloat ? TINY_TOK_FLOAT : TINY_TOK_INT;
    }

#define CHECK_ESCAPE()              \
    do {                            \
        if (l->last == '\\') {      \
            l->last = GetChar(l);   \
            switch (l->last) {      \
                case '"':           \
                    l->last = '"';  \
                    break;          \
                case '\'':          \
                    l->last = '\''; \
                    break;          \
                case 't':           \
                    l->last = '\t'; \
                    break;          \
                case 'n':           \
                    l->last = '\n'; \
                    break;          \
                case 'r':           \
                    l->last = '\r'; \
                    break;          \
                case 'b':           \
                    l->last = '\b'; \
                    break;          \
            }                       \
        }                           \
    } while (0)

    if (l->last == '\'') {
        l->last = GetChar(l);

        CHECK_ESCAPE();

        l->iValue = l->last;

        l->last = GetChar(l);

        if (l->last != '\'') {
            ReportError(l, "Expected ' to close previous '.");
        }

        l->last = GetChar(l);

        return TINY_TOK_CHAR;
    }

    if (l->last == '"') {
        ResetLexeme(l);

        l->last = GetChar(l);

        while (l->last != '"') {
            CHECK_ESCAPE();

            sb_push(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        sb_push(l->lexeme, 0);

        l->last = GetChar(l);

        return TINY_TOK_STRING;
    }

    ReportError(l, "Unexpected character '%c'.", l->last);
    return -1;
}

void Tiny_DestroyLexer(Tiny_Lexer *l) { sb_free(l->lexeme); }

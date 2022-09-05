#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t TokenPos;

// TODO(Apaar): Translate token pos to line number given src

typedef enum {
    TOK_OPENPAREN,
    TOK_CLOSEPAREN,
    TOK_OPENCURLY,
    TOK_CLOSECURLY,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_GT,
    TOK_LT,
    TOK_EQUAL,
    TOK_BANG,
    TOK_AND,
    TOK_OR,
    TOK_COMMA,
    TOK_SEMI,
    TOK_COLON,
    TOK_DOT,

    TOK_LOG_AND,  // &&
    TOK_LOG_OR,   // ||

    TOK_DECLARE,       // :=
    TOK_DECLARECONST,  // ::

    TOK_PLUSEQUAL,     // +=
    TOK_MINUSEQUAL,    // -=
    TOK_STAREQUAL,     // *=
    TOK_SLASHEQUAL,    // /=
    TOK_PERCENTEQUAL,  // %=
    TOK_OREQUAL,       // |=
    TOK_ANDEQUAL,      // &=

    TOK_EQUALS,
    TOK_NOTEQUALS,
    TOK_LTE,
    TOK_GTE,

    TOK_NULL,
    TOK_BOOL,
    TOK_CHAR,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,

    TOK_IDENT,

    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_FUNC,
    TOK_FOREIGN,
    TOK_STRUCT,
    TOK_NEW,
    TOK_CAST,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_IMPORT,

    TOK_EOF,

    TOK_ERROR
} TokenType;

typedef struct Lexer {
    Tiny_Context* ctx;

    const char* fileName;
    int lineNumber;

    const char* src;
    size_t len;

    TokenPos pos;

    int last;

    // Buffer
    char* lexeme;

    char* errorMessage;

    union {
        bool bValue;
        int iValue;
        float fValue;
    };
} Lexer;

static int GetChar(Lexer* l) { return l->pos >= l->len ? 0 : l->src[l->pos++]; }

static int Peek(Lexer* l) { return l->src[l->pos]; }

static void InitLexer(Lexer* l, Tiny_Context* ctx, const char* fileName, const char* src,
                      size_t len) {
    l->ctx = ctx;

    l->fileName = fileName;
    l->lineNumber = 1;

    l->src = src;
    l->len = len;

    l->pos = 0;

    l->last = ' ';

    l->errorMessage = NULL;

    INIT_BUF(l->lexeme, ctx);
}

static void DestroyLexer(Lexer* l) {
    if (l->errorMessage) {
        TFree(l->ctx, l->errorMessage);
        l->errorMessage = NULL;
    }

    DESTROY_BUF(l->lexeme);
}

#define LEXER_ERROR(l, fmt, ...) \
    (((l)->errorMessage = MemPrintf((l)->ctx, (fmt), ##__VA_ARGS__)), TOK_ERROR)

static TokenType GetToken(Lexer* l) {
    // Make sure that error has been handled
    assert(!l->errorMessage);

    while (isspace(l->last)) {
        if (l->last == '\n') l->lineNumber++;
        l->last = GetChar(l);
    }

    if (l->last == 0) {
        return TOK_EOF;
    }

    if (l->last == '/' && Peek(l) == '/') {
        while (l->last && l->last != '\n') {
            l->last = GetChar(l);
        }

        if (l->last) {
            l->last = GetChar(l);
            l->lineNumber += 1;
        }

        return GetToken(l);
    }

#define MATCH2(s, tok)                            \
    do {                                          \
        if (l->last == s[0] && Peek(l) == s[1]) { \
            BUF_CLEAR(l->lexeme);                 \
            BUF_PUSH(l->lexeme, s[0]);            \
            BUF_PUSH(l->lexeme, s[1]);            \
            BUF_PUSH(l->lexeme, 0);               \
            l->pos += 1;                          \
            l->last = GetChar(l);                 \
            return TOK_##tok;                     \
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
    MATCH2("%=", PERCENTEQUAL);
    MATCH2("|=", OREQUAL);
    MATCH2("&=", ANDEQUAL);

    MATCH2("==", EQUALS);
    MATCH2("!=", NOTEQUALS);

    MATCH2("<=", LTE);
    MATCH2(">=", GTE);

#define MATCH(c, tok)               \
    do {                            \
        if (l->last == c) {         \
            BUF_CLEAR(l->lexeme);   \
            BUF_PUSH(l->lexeme, c); \
            BUF_PUSH(l->lexeme, 0); \
            l->last = GetChar(l);   \
            return TOK_##tok;       \
        }                           \
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
        BUF_CLEAR(l->lexeme);

        while (isalnum(l->last) || l->last == '_') {
            BUF_PUSH(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        BUF_PUSH(l->lexeme, 0);

        if (strcmp(l->lexeme, "null") == 0) return TOK_NULL;

        if (strcmp(l->lexeme, "true") == 0) {
            l->bValue = true;
            return TOK_BOOL;
        }

        if (strcmp(l->lexeme, "false") == 0) {
            l->bValue = false;
            return TOK_BOOL;
        }

        if (strcmp(l->lexeme, "if") == 0) return TOK_IF;
        if (strcmp(l->lexeme, "func") == 0) return TOK_FUNC;
        if (strcmp(l->lexeme, "foreign") == 0) return TOK_FOREIGN;
        if (strcmp(l->lexeme, "return") == 0) return TOK_RETURN;
        if (strcmp(l->lexeme, "while") == 0) return TOK_WHILE;
        if (strcmp(l->lexeme, "for") == 0) return TOK_FOR;
        if (strcmp(l->lexeme, "else") == 0) return TOK_ELSE;
        if (strcmp(l->lexeme, "struct") == 0) return TOK_STRUCT;
        if (strcmp(l->lexeme, "new") == 0) return TOK_NEW;
        if (strcmp(l->lexeme, "cast") == 0) return TOK_CAST;
        if (strcmp(l->lexeme, "break") == 0) return TOK_BREAK;
        if (strcmp(l->lexeme, "continue") == 0) return TOK_CONTINUE;
        if (strcmp(l->lexeme, "import") == 0) return TOK_IMPORT;

        return TOK_IDENT;
    }

    if (isdigit(l->last)) {
        BUF_CLEAR(l->lexeme);

        bool isFloat = false;

        while (isdigit(l->last) || (l->last == '.' && !isFloat)) {
            if (l->last == '.') isFloat = true;

            BUF_PUSH(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        BUF_PUSH(l->lexeme, 0);

        if (isFloat) {
            l->fValue = strtof(l->lexeme, NULL);
        } else {
            l->iValue = strtol(l->lexeme, NULL, 10);
        }

        return isFloat ? TOK_FLOAT : TOK_INT;
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
            return LEXER_ERROR(l, "Expected ' to close previous '.");
        }

        l->last = GetChar(l);

        return TOK_CHAR;
    }

    if (l->last == '"') {
        BUF_CLEAR(l->lexeme);

        l->last = GetChar(l);

        while (l->last != '"') {
            CHECK_ESCAPE();

            BUF_PUSH(l->lexeme, l->last);
            l->last = GetChar(l);
        }

        BUF_PUSH(l->lexeme, 0);

        l->last = GetChar(l);

        return TOK_STRING;
    }

    return LEXER_ERROR(l, "Unexpected character '%c'.", l->last);
}

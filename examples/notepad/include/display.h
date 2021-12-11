#pragma once

#define MAX_STATUS_LENGTH 256
#define LINES_PER_PAGE 40

typedef struct Tigr Tigr;
typedef struct Editor Editor;

typedef enum {
    TOK_DIRECTIVE,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_SPACE,
    TOK_STRING,
    TOK_CHAR,
    TOK_NUM,
    TOK_DEFINITION,
    TOK_COMMENT,
    TOK_SPECIAL_COMMENT,
    NUM_TOKEN_TYPES,

    TOK_MULTILINE_COMMENT_START,  // Just the /*
    TOK_MULTILINE_COMMENT_END,    // Just the */
} TokenType;

extern char Status[MAX_STATUS_LENGTH];

void SetTokenColor(TokenType type, int r, int g, int b);
void DrawEditor(Tigr* screen, Editor* editor);

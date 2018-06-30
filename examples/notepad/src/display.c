#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "editor.h"
#include "tigr.h"
#include "display.h"
#include "buffer.h"

#define MAX_TOKEN_LENGTH 256
#define MAX_TOKENS 128

typedef struct
{
    TokenType type;
    char lexeme[MAX_TOKEN_LENGTH];
} Token;

char Status[MAX_STATUS_LENGTH];

#define RGB(r, g, b) { b, g, r, 255 }

static TPixel TokenColors[NUM_TOKEN_TYPES] = {
    RGB(60, 60, 60),
    RGB(200, 200, 200),
    RGB(40, 40, 150),
    RGB(0, 0, 0),
    RGB(40, 150, 40),
    RGB(130, 130, 130),
    RGB(150, 150, 40),
    RGB(150, 30, 150),
    RGB(20, 80, 20),
    RGB(200, 120, 40)
};

// Returns number of tokens extracted
static int Tokenize(const Buffer* buf, const char* line, Token* tokens, int maxTokens)
{
    // TODO(Apaar): Check for buffer overflows
    int curTok = 0;

    const char* start = line;

    while (*line) {
        if (*line == '#') {
            tokens[curTok].type = TOK_DIRECTIVE;

            int i = 0;
            while (*line == '#' || isalpha(*line)) {
                tokens[curTok].lexeme[i++] = *line++;
            }

            tokens[curTok].lexeme[i] = '\0';
            curTok += 1;
        } else if (isascii(*line) && isdigit(*line)) {
            tokens[curTok].type = TOK_NUM;
            int i = 0;

            while (isdigit(*line) || *line == '.' || isxdigit(*line)) {
                tokens[curTok].lexeme[i++] = *line++;
            }

            tokens[curTok].lexeme[i] = '\0';
            curTok += 1;
        } else if (isascii(*line) && isalpha(*line)) {
            tokens[curTok].type = TOK_IDENT;
            int i = 0;
            
            while (isalnum(*line) || *line == '_') {
                tokens[curTok].lexeme[i++] = *line++;
                if (!isascii(*line)) {
                    break;
                }
            }

            tokens[curTok].lexeme[i] = '\0';
            
            const char* lexeme = tokens[curTok].lexeme;

            if ((strcmp(lexeme, "if") == 0) ||
                (strcmp(lexeme, "typedef") == 0) ||
                (strcmp(lexeme, "const") == 0) ||
                (strcmp(lexeme, "while") == 0) ||
                (strcmp(lexeme, "for") == 0) ||
                (strcmp(lexeme, "else") == 0) ||
                (strcmp(lexeme, "do") == 0) ||
                (strcmp(lexeme, "switch") == 0) ||
                (strcmp(lexeme, "struct") == 0) ||
                (strcmp(lexeme, "enum") == 0) ||
                (strcmp(lexeme, "char") == 0) ||
                (strcmp(lexeme, "int") == 0) ||
                (strcmp(lexeme, "void") == 0) ||
                (strcmp(lexeme, "unsigned") == 0) ||
                (strcmp(lexeme, "bool") == 0) ||
                (strcmp(lexeme, "short") == 0) ||
                (strcmp(lexeme, "long") == 0) ||
                (strcmp(lexeme, "float") == 0) ||
                (strcmp(lexeme, "false") == 0) ||
                (strcmp(lexeme, "true") == 0) ||
                (strcmp(lexeme, "static") == 0) ||
                (strcmp(lexeme, "return") == 0) ||
                (strcmp(lexeme, "func") == 0) ||
                (strcmp(lexeme, "or") == 0) ||
                (strcmp(lexeme, "and") == 0) ||
                (strcmp(lexeme, "not") == 0) ||
                (strcmp(lexeme, "case") == 0)) {
                tokens[curTok].type = TOK_KEYWORD;
            }

            for (int i = 0; i < buf->numDefns; ++i) {
                if (strcmp(buf->defns[i], lexeme) == 0) {
                    tokens[curTok].type = TOK_DEFINITION;
                }
            }

            curTok += 1;
        } else if (isascii(*line) && isspace(*line)) {
            tokens[curTok].type = TOK_SPACE;
            int i = 0;

            while (isspace(*line)) {
                tokens[curTok].lexeme[i++] = *line++;
            }

            tokens[curTok].lexeme[i] = '\0';
            curTok += 1;
        } else if(*line == '"') {
            line += 1;
            tokens[curTok].type = TOK_STRING;
            int i = 0;

            tokens[curTok].lexeme[i++] = '"';

            while (*line && *line != '"') {
                tokens[curTok].lexeme[i++] = *line++;
            }

            if (*line == '"') {
                tokens[curTok].lexeme[i++] = '"';
                line += 1;
            }

            tokens[curTok].lexeme[i] = '\0';
            curTok += 1;
        } else if (*line == '/' && line[1] == '/') {
            line += 2;

            int i = 0;

            tokens[curTok].type = TOK_COMMENT;

            tokens[curTok].lexeme[i++] = '/';
            tokens[curTok].lexeme[i++] = '/';

            while (*line) {
                tokens[curTok].lexeme[i++] = *line++;
            }

            if (strstr(tokens[curTok].lexeme, "TODO") || strstr(tokens[curTok].lexeme, "NOTE")) {
                tokens[curTok].type = TOK_SPECIAL_COMMENT;
            }

            tokens[curTok].lexeme[i] = '\0';
            curTok += 1;
        } else if(*line == '/' && line[1] == '*') {
            tokens[curTok].type = TOK_MULTILINE_COMMENT_START;

            tokens[curTok].lexeme[0] = '/';
            tokens[curTok].lexeme[1] = '*';
            tokens[curTok].lexeme[2] = 0;

            line += 2;

            curTok += 1;
        } else if(*line == '*' && line[1] == '/') {
            tokens[curTok].type = TOK_MULTILINE_COMMENT_END;

            tokens[curTok].lexeme[0] = '*';
            tokens[curTok].lexeme[1] = '/';
            tokens[curTok].lexeme[2] = 0;

            line += 2;

            curTok += 1;
        } else {
            if (!isascii(*line)) {
                tokens[curTok].type = TOK_CHAR;
                tokens[curTok].lexeme[0] = 223;
                tokens[curTok].lexeme[1] = '\0';
                curTok += 1;

                line += 1;
                continue;
            }

            tokens[curTok].type = TOK_CHAR;
            tokens[curTok].lexeme[0] = *line++;
            tokens[curTok].lexeme[1] = '\0';

            curTok += 1;
        }
    }

    return curTok;
}

void SetTokenColor(TokenType type, int r, int g, int b)
{
    TokenColors[type] = tigrRGB(r, g, b);
}

void DrawEditor(Tigr* screen, Editor* ed)
{
    int y = 0;

    const Buffer* buf = &ed->buf;

    TigrFont* font = tfont;

    const TPixel* tokenColors = TokenColors;

    // Whether we're inside a multiline comment
    bool insideComment = false;

    bool blink = ed->blinkTime < 0.5f;

    for(int i = ed->scrollY; i < buf->numLines; ++i)  {
        if(y >= screen->h) {
            break;
        }

        int x = 0;
        int drawCurX = 0;

        // Show line numbers
        char lbuf[32];
        sprintf(lbuf, "%*d", (int)ceil(log10(buf->numLines)), i + 1);

        tigrPrint(screen, font, x, y, tigrRGB(40, 40, 40), lbuf);
        x += tigrTextWidth(font, lbuf) + 4;

        if(ed->mode == MODE_VISUAL_LINE) {
            int a = ed->vStart.y;
            int b = ed->cur.y;

            if(a > b) {
                int temp = b;
                b = a;
                a = temp;
            }

            if(i >= a && i <= b) {
                tigrFill(screen, x, y, tigrTextWidth(font, buf->lines[i]), tigrTextHeight(font, buf->lines[i]), tigrRGB(80, 80, 80));
            }
        }

        int lineLen = strlen(buf->lines[i]);

        // TODO(Apaar): Don't bother tokenizing when 
        // the filetype is unknown (or maybe just produce line-long
        // tokens)

        static Token tokens[MAX_TOKENS];

        int numTokens = Tokenize(buf, buf->lines[i], tokens, MAX_TOKENS);

        if(i == ed->cur.y) {
            assert(ed->cur.x >= 0 && ed->cur.x <= lineLen);

            if(blink) {
                if(numTokens == 0) {
                    int w = tigrTextWidth(font, " ");
                    int h = tigrTextHeight(font, "A");

                    if(ed->mode == MODE_INSERT) {
                        w = 2;
                    }

                    tigrFill(screen, x, y, w, h, tigrRGB(100, 100, 100));
                } else if(ed->cur.x == lineLen) {
                    assert(ed->mode == MODE_INSERT);

                    int w = tigrTextWidth(font, buf->lines[ed->cur.y]);
                    int h = tigrTextHeight(font, buf->lines[ed->cur.y]);

                    tigrFill(screen, x + w, y, 2, h, tigrRGB(100, 100, 100));
                }
            }
        }

        for(int j = 0; j < numTokens; ++j) {
            int len = 0;
            int k = 0;

            if(tokens[j].type == TOK_MULTILINE_COMMENT_START) {
                insideComment = true;
            } 

            const char* s = tokens[j].lexeme;

            static char lexeme[MAX_TOKEN_LENGTH];

            while(*s) {
                if(*s == '%') {
                    lexeme[k++] = '%';
                    lexeme[k++] = '%';
                    s += 1;
                } else {
                    lexeme[k++] = *s++;
                }
                len += 1;
            }
            lexeme[k] = 0;

            TPixel color = tokenColors[tokens[j].type];

            if(insideComment) {
                color = tokenColors[TOK_COMMENT];
            }

            int lexw = tigrTextWidth(font, lexeme);
           
            if(x + lexw >= screen->w) {
                // Wrap once
                // anything longer is obnoxiously long and should be deleted
                x = 0;
                y += tigrTextHeight(font, lexeme);
            }

            tigrPrint(screen, font, x, y, color, lexeme);

            if (tokens[j].type == TOK_MULTILINE_COMMENT_END) {
                insideComment = false;
            }

            if(blink && i == ed->cur.y && drawCurX <= ed->cur.x && ed->cur.x < drawCurX + len) {
                char buf[MAX_TOKEN_LENGTH];
                strcpy(buf, tokens[j].lexeme);

                buf[ed->cur.x - drawCurX] = '\0';

                int xx = tigrTextWidth(font, buf);

                strcpy(buf, tokens[j].lexeme);

                buf[1] = '\0';

                int w = tigrTextWidth(font, buf);
                int h = tigrTextHeight(font, buf);

                if (ed->mode == MODE_INSERT) {
                    w = 2;
                }

                tigrFill(screen, xx + x, y, w, h, tigrRGB(100, 100, 100));
            }

            x += tigrTextWidth(font, tokens[j].lexeme);
            drawCurX += len;
        }

        y += tigrTextHeight(font, buf->lines[i]);
    }

    if(Status[0] || ed->mode == MODE_COMMAND || ed->mode == MODE_FORWARD_SEARCH) {
        // print status/command
        static char buf[MAX_COMMAND_LENGTH];
        
        if(ed->mode == MODE_COMMAND) {
            sprintf(buf, ":%s", ed->cmd);
        } else if(ed->mode == MODE_FORWARD_SEARCH) {
            sprintf(buf, "/%s", ed->cmd);
        } else {
            strcpy(buf, Status);
        }

        int h = tigrTextHeight(font, buf);
        tigrFill(screen, 0, screen->h - h, screen->w, h, tigrRGB(0, 0, 0));
        tigrPrint(screen, font, 0, screen->h - h, tigrRGB(200, 200, 200), buf);
    }
}


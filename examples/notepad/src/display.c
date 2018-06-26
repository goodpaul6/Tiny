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

typedef enum
{
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
    NUM_TOKEN_TYPES
} TokenType;

typedef struct
{
    TokenType type;
    char lexeme[MAX_TOKEN_LENGTH];
} Token;

char Status[MAX_STATUS_LENGTH];

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
		} else if (isdigit(*line)) {
			tokens[curTok].type = TOK_NUM;
			int i = 0;

			while (isdigit(*line) || *line == '.' || isxdigit(*line)) {
                tokens[curTok].lexeme[i++] = *line++;
            }

            tokens[curTok].lexeme[i] = '\0';
			curTok += 1;
		} else if (isalpha(*line)) {
            tokens[curTok].type = TOK_IDENT;
            int i = 0;

            while (isalnum(*line) || *line == '_') {
                tokens[curTok].lexeme[i++] = *line++;
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
				(strcmp(lexeme, "return") == 0)) {
				tokens[curTok].type = TOK_KEYWORD;
			}

			for (int i = 0; i < buf->numDefns; ++i) {
				if (strcmp(buf->defns[i], lexeme) == 0) {
					tokens[curTok].type = TOK_DEFINITION;
				}
			}

            curTok += 1;
        } else if (isspace(*line)) {
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
		} else {
            tokens[curTok].type = TOK_CHAR;
            tokens[curTok].lexeme[0] = *line++;
            tokens[curTok].lexeme[1] = '\0';

            curTok += 1;
        }
    }

    return curTok;
}

void DrawEditor(Tigr* screen, Editor* ed)
{
    int y = 0;

    const Buffer* buf = &ed->buf;

    TigrFont* font = tfont;

    const TPixel tokenColors[NUM_TOKEN_TYPES] = {
        tigrRGB(60, 60, 60),
        tigrRGB(200, 200, 200),
        tigrRGB(40, 40, 150),
        tigrRGB(0, 0, 0),
        tigrRGB(40, 150, 40),
        tigrRGB(130, 130, 130),
        tigrRGB(150, 150, 40),
        tigrRGB(150, 30, 150),
        tigrRGB(20, 80, 20),
        tigrRGB(200, 120, 40)
    };

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

            if(ed->blink) {
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

            tigrPrint(screen, font, x, y, tokenColors[tokens[j].type], lexeme);

            if(ed->blink && i == ed->cur.y && drawCurX <= ed->cur.x && ed->cur.x < drawCurX + len) {
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

	if(Status[0]) {
		// print status
		int h = tigrTextHeight(font, Status);
		tigrFill(screen, 0, screen->h - h, screen->w, h, tigrRGB(0, 0, 0));
		tigrPrint(screen, font, 0, screen->h - h, tigrRGB(200, 200, 200), Status);
	}
}

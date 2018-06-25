#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include "tigr.h"

#define MAX_NUM_LINES		1024
#define MAX_LINE_LENGTH		512
#define MAX_TOKEN_LENGTH	64
#define MAX_TOKENS			512
#define WIDTH				640
#define HEIGHT				480

typedef enum
{
    TOK_DIRECTIVE,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_SPACE,
    TOK_STRING,
    TOK_CHAR,
	TOK_NUM,
    NUM_TOKEN_TYPES
} TokenType;

typedef struct
{
    TokenType type;
    char lexeme[MAX_TOKEN_LENGTH];
} Token;

typedef struct
{
    bool highlight;
    int numLines;
    char lines[MAX_NUM_LINES][MAX_LINE_LENGTH];
} Buffer;

static Buffer GBuffer;

static void Log(const char* s, ...)
{
    va_list args;
    va_start(args, s);

    vprintf(s, args);

    va_end(args);
}

static void OpenFile(const char* filename)
{
    Log("Opening file: %s\n", filename);

    FILE* f = fopen(filename, "r");

    int last = getc(f);

	GBuffer.highlight = false;
	
	const char* ext = strrchr(filename, '.');
	if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cc") == 0 || strcmp(ext, ".h") == 0 || 
		strcmp(ext, ".hh") == 0 || strcmp(ext, ".hpp") == 0)) {
		GBuffer.highlight = true;
	}

    int curChar = 0;
    int curLine = 0;

    while (last != EOF) {
        if (last == '\n') {
            GBuffer.lines[curLine][curChar] = '\0';
            curLine += 1;
            curChar = 0;
        } else {
            if (curLine >= MAX_NUM_LINES) {
                Log("File exceeded maximum number of lines. Only partially loaded.");

                GBuffer.numLines = curLine;
                fclose(f);
                return;
            }

            if (curChar >= MAX_LINE_LENGTH - 1) {
                continue;
            }

			if (last == '\t') {
				// Translate tabs to 4 spaces
				for (int i = 0; i < 4; ++i) {
					GBuffer.lines[curLine][curChar++] = ' ';
				}
			} else {
				GBuffer.lines[curLine][curChar++] = last;
			}
        }

        last = getc(f);
    }

    GBuffer.numLines = curLine + 1;

    fclose(f);
}

// Returns number of tokens extracted
static int Tokenize(const char* line, Token* tokens, int maxTokens)
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

			while (isdigit(*line) || *line == '.') {
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
				(strcmp(lexeme, "unsigned") == 0) ||
				(strcmp(lexeme, "bool") == 0) ||
				(strcmp(lexeme, "short") == 0) ||
				(strcmp(lexeme, "long") == 0)) {
				tokens[curTok].type = TOK_KEYWORD;
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

int main(int argc, char** argv)
{
    if(argc > 1) {
        OpenFile(argv[1]);
    }

    Tigr* screen = tigrWindow(WIDTH, HEIGHT, "Tiny Notepad", TIGR_FIXED | TIGR_2X);

    const TPixel tokenColors[NUM_TOKEN_TYPES] = {
        tigrRGB(60, 60, 60),
        tigrRGB(200, 200, 200),
        tigrRGB(40, 40, 150),
        tigrRGB(0, 0, 0),
        tigrRGB(40, 150, 40),
        tigrRGB(130, 130, 130),
        tigrRGB(150, 150, 40)
    };

	int scrollY = 0;
	int curX = 0, curY = 0;
	int lastMaxLine = 0;

	float elapsed = tigrTime();

    while(!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));

        int y = 0;

		elapsed += tigrTime();

		if (tigrKeyDown(screen, TK_LEFT) && curX > 0) {
			--curX;
		}

		if (tigrKeyDown(screen, TK_RIGHT) && curX < strlen(GBuffer.lines[curY]) - 1) {
			++curX;
		}

		if (tigrKeyDown(screen, TK_UP) && curY > 0) {
			--curY;
		}

		if (tigrKeyDown(screen, TK_DOWN) && curY < GBuffer.numLines - 1) {
			++curY;
		}

        for(int i = scrollY; i < GBuffer.numLines; ++i) {
			if (y >= HEIGHT) {
				lastMaxLine = i;
				break;
			}

			int drawCurX = 0;
            int x = 0;

            static Token tokens[MAX_TOKENS];

            int numTokens = Tokenize(GBuffer.lines[i], tokens, MAX_TOKENS);

			if (i == curY && numTokens == 0 && elapsed > 0.5f) {
				// Move/draw the cursor at 0
				curX = 0;

				int w = tigrTextWidth(tfont, " ");
				int h = tigrTextHeight(tfont, "A");
				tigrFill(screen, 0, y, w, h, tigrRGB(100, 100, 100));
			}

            for (int j = 0; j < numTokens; ++j) {
				int len = strlen(tokens[j].lexeme);

                tigrPrint(screen, tfont, x, y, tokenColors[tokens[j].type], tokens[j].lexeme);

				if (elapsed > 0.5f && i == curY && drawCurX <= curX && curX < drawCurX + len) {
					char buf[MAX_TOKEN_LENGTH];
					strcpy(buf, tokens[j].lexeme);

					buf[curX - drawCurX] = '\0';

					int xx = tigrTextWidth(tfont, buf);

					strcpy(buf, tokens[j].lexeme);

					buf[1] = '\0';

					int w = tigrTextWidth(tfont, buf);
					int h = tigrTextHeight(tfont, buf);

					tigrFill(screen, xx + x, y, w, h, tigrRGB(100, 100, 100));
				}

                x += tigrTextWidth(tfont, tokens[j].lexeme);
				drawCurX += strlen(tokens[j].lexeme);
            }

            y += tigrTextHeight(tfont, GBuffer.lines[i]);
        }

		if (curY < scrollY) {
			scrollY -= 1;
		}

		if (curY >= lastMaxLine) {
			++scrollY;
		}
		
		if (elapsed > 1.0f) {
			elapsed = tigrTime();
		}

        tigrUpdate(screen);
    }

    return 0;
}

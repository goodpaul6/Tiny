#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include "tigr.h"

#define MAX_NUM_LINES		2048
#define MAX_LINE_LENGTH		512
#define MAX_TOKEN_LENGTH	64
#define MAX_TOKENS			512
#define WIDTH				640
#define HEIGHT				480

typedef enum
{
	MODE_NORMAL,
	MODE_NORMAL_TO_INSERT,
	MODE_INSERT,
} Mode;

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
				(strcmp(lexeme, "long") == 0) ||
				(strcmp(lexeme, "false") == 0) ||
				(strcmp(lexeme, "true") == 0) ||
				(strcmp(lexeme, "static") == 0)) {
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
			tokens[curTok].lexeme[i++] = '"';

			line += 1;

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

// Basically backspace (will move around yp if backspace moves up a line)
static void RemoveChar(int* xp, int* yp)
{
	int x = *xp;
	int y = *yp;
	
	assert(y >= 0 && y < GBuffer.numLines);
	int len = strlen(GBuffer.lines[y]);
	assert(x >= 0 && x <= len);
	
	if (x > 0) {
		if (x == len) {
			GBuffer.lines[y][x - 1] = '\0';
		}
		else {
			memmove(&GBuffer.lines[y][x - 1], &GBuffer.lines[y][x], len - x);
			GBuffer.lines[y][len - 1] = '\0';
		}

		*xp -= 1;
	} else if (y > 0 && GBuffer.numLines > 1) {
		// Move the contents of the rest of this line to the end of the previous line
		// and put the cursor there

		*xp = strlen(GBuffer.lines[y - 1]);
		strcat(GBuffer.lines[y - 1], GBuffer.lines[y]);

		// Shift all other lines up
		memmove(&GBuffer.lines[y], GBuffer.lines[y + 1], (GBuffer.numLines - y) * sizeof(GBuffer.lines[0]));
		GBuffer.numLines -= 1;
		*yp -= 1;
	}
}

static void InsertNewline(int* xp, int* yp)
{
	int x = *xp;
	int y = *yp;

	assert(y >= 0 && y < GBuffer.numLines);
	int len = strlen(GBuffer.lines[y]);
	assert(x >= 0 && x <= len);

	*yp += 1;
	y = *yp;

	if (y >= GBuffer.numLines) {
		assert(GBuffer.numLines < MAX_NUM_LINES);
		GBuffer.lines[y][0] = '\0';
		GBuffer.numLines = y + 1;
	}
	else {
		// Shift all following lines over
		memmove(&GBuffer.lines[y + 1], &GBuffer.lines[y], (GBuffer.numLines - y) * sizeof(GBuffer.lines[0]));

		GBuffer.lines[y][0] = '\0';

		// Move all the contents of the rest of the current line down into the new line
		strcat(GBuffer.lines[y], &GBuffer.lines[y - 1][x]);
		GBuffer.lines[y - 1][x] = '\0';

		GBuffer.numLines += 1;
	}

	*xp = 0;
}

static void InsertChar(char ch, int* xp, int* yp)
{
	int x = *xp;
	int y = *yp;

	assert(y >= 0 && y < GBuffer.numLines);
	int len = strlen(GBuffer.lines[y]);
	assert(x >= 0 && x <= len);

	if (x == len) {
		GBuffer.lines[y][x] = ch;
		GBuffer.lines[y][x + 1] = '\0';
	} else {
		memmove(&GBuffer.lines[y][x + 1], &GBuffer.lines[y][x], len - x);
		GBuffer.lines[y][x] = ch;
		GBuffer.lines[y][len + 1] = '\0';
	}

	*xp += 1;
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

	Mode mode = MODE_NORMAL;

	float elapsed = tigrTime();
	float lastReleaseTime[8] = { 0 };

    while(!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));

        int y = 0;

		const float repeatTime = 0.6f;

		elapsed += tigrTime();
		int ticks = (int)(elapsed / 0.5f);

		bool left = tigrKeyDown(screen, TK_LEFT) || tigrKeyDown(screen, 'H');
		bool right = tigrKeyDown(screen, TK_RIGHT) || tigrKeyDown(screen, 'L');
		bool up = tigrKeyDown(screen, TK_UP) || tigrKeyDown(screen, 'K');
		bool down = tigrKeyDown(screen, TK_DOWN) || tigrKeyDown(screen, 'J');

		// Key repeat
		if (!tigrKeyHeld(screen, TK_LEFT) && !tigrKeyHeld(screen, 'H')) {
			lastReleaseTime[0] = elapsed;
		}

		if (!tigrKeyHeld(screen, TK_RIGHT) && !tigrKeyHeld(screen, 'L')) {
			lastReleaseTime[1] = elapsed;
		}

		if (!tigrKeyHeld(screen, TK_UP) && !tigrKeyHeld(screen, 'K')) {
			lastReleaseTime[2] = elapsed;
		}

		if (!tigrKeyHeld(screen, TK_DOWN) && !tigrKeyHeld(screen, 'J')) {
			lastReleaseTime[3] = elapsed;
		}

		bool blink = ticks % 2 == 0;

		// Movement
		if (mode == MODE_NORMAL) {
			if ((left || (elapsed - lastReleaseTime[0]) > repeatTime) && curX > 0) {
				--curX;
				blink = true;
			}

			if ((right || (elapsed - lastReleaseTime[1]) > repeatTime) && curX < strlen(GBuffer.lines[curY]) - 1) {
				++curX;
				blink = true;
			}

			if ((up || (elapsed - lastReleaseTime[2]) > repeatTime) && curY > 0) {
				--curY;
				blink = true;
			}

			if ((down || (elapsed - lastReleaseTime[3] > repeatTime)) && curY < GBuffer.numLines - 2) {
				++curY;
				blink = true;
			}

			if (tigrKeyDown(screen, 'E')) {
				// Move to end of word
				const char* cur = &GBuffer.lines[curY][curX];

				if (cur[1] == ' ') {
					// Space right after, move up
					curX += 1;
					cur += 1;
					curX += strspn(cur, " ");
				}
				else {
					const char* spc = strchr(cur, ' ');
					if (spc) {
						curX += spc - cur - 1;
					}
					else {
						curX += strlen(cur) - 1;
					}
				}
			}

			if (tigrKeyDown(screen, 'B')) {
				// Move to start of word
				const char* cur = &GBuffer.lines[curY][curX];

				if (curX > 0 && cur[-1] == ' ') {
					// Space right before, move back
					curX -= 1;
					cur -= 1;
					while (*cur == ' ') {
						--curX;
						--cur;
					}

					while (curX > 0 && *cur != ' ') {
						--curX;
						--cur;
					}
				}
				else {
					while (curX > 0 && cur[-1] != ' ') {
						--curX;
						--cur;
					}
				}
			}

			if (tigrKeyDown(screen, 'I')) {
				if (tigrKeyHeld(screen, TK_SHIFT)) {
					curX = 0;
				}

				mode = MODE_NORMAL_TO_INSERT;
			}

			if (tigrKeyDown(screen, 'A')) {
				if (tigrKeyHeld(screen, TK_SHIFT)) {
					curX = strlen(GBuffer.lines[curY]);
				} else {
					curX += 1;
				}

				mode = MODE_NORMAL_TO_INSERT;
			}

			if (tigrKeyDown(screen, 'O')) {
				if (tigrKeyHeld(screen, TK_SHIFT)) {
					curX = 0;
					InsertNewline(&curX, &curY);
					curY -= 1;
				} else {
					curX = strlen(GBuffer.lines[curY]);
					InsertNewline(&curX, &curY);
				}

				mode = MODE_NORMAL_TO_INSERT;
			}

			if (tigrKeyDown(screen, 'G') && tigrKeyHeld(screen, TK_SHIFT)) {
				scrollY = GBuffer.numLines - (lastMaxLine - scrollY) - 1;
				curY = GBuffer.numLines - 1;
			}
		} else if(mode == MODE_INSERT) {
			if (tigrKeyDown(screen, TK_ESCAPE)) {
				mode = MODE_NORMAL;
			} else {
				int value = tigrReadChar(screen);
				
				if (value == '\b') {
					RemoveChar(&curX, &curY);
				} else if (value == '\n') {
					InsertNewline(&curX, &curY);
				} else if (value > 0) {
					int len = strlen(GBuffer.lines[curY]);
					
					if (value == '\t') {
						for (int i = 0; i < 4; ++i) {
							InsertChar(' ', &curX, &curY);
						}
					} else {
						InsertChar(value, &curX, &curY);
					}
				}
			}
		} else if (mode == MODE_NORMAL_TO_INSERT) {
			char key = tigrReadChar(screen);
			mode = MODE_INSERT;
		}

        for(int i = scrollY; i < GBuffer.numLines; ++i) {
			if (y >= HEIGHT) {
				lastMaxLine = i;
				break;
			}

			int drawCurX = 0;
            int x = 0;

			int lineLen = strlen(GBuffer.lines[i]);

			if (i == curY) {
				if (mode == MODE_NORMAL) {
					if (curX >= lineLen) {
						// Move back to bounds of line
						curX = lineLen - 1;
					}
				} else if(mode == MODE_INSERT) {
					if (curX > lineLen) {
						curX = lineLen;
					}
				}
			}

            static Token tokens[MAX_TOKENS];

            int numTokens = Tokenize(GBuffer.lines[i], tokens, MAX_TOKENS);

			if (i == curY && blink) {
				if (numTokens == 0) {
					// Move/draw the cursor at 0
					curX = 0;

					int w = tigrTextWidth(tfont, " ");
					int h = tigrTextHeight(tfont, "A");

					if (mode == MODE_INSERT) {
						w = 2;
					}

					tigrFill(screen, 0, y, w, h, tigrRGB(100, 100, 100));
				} else if(curX == lineLen) {
					assert(mode != MODE_NORMAL);

					int w = tigrTextWidth(tfont, GBuffer.lines[curY]);
					int h = tigrTextHeight(tfont, GBuffer.lines[curY]);

					tigrFill(screen, w, y, 2, h, tigrRGB(100, 100, 100));
				}
			}

            for (int j = 0; j < numTokens; ++j) {
				int len = strlen(tokens[j].lexeme);

                tigrPrint(screen, tfont, x, y, tokenColors[tokens[j].type], tokens[j].lexeme);

				if (blink && i == curY && drawCurX <= curX && curX < drawCurX + len) {
					char buf[MAX_TOKEN_LENGTH];
					strcpy(buf, tokens[j].lexeme);

					buf[curX - drawCurX] = '\0';

					int xx = tigrTextWidth(tfont, buf);

					strcpy(buf, tokens[j].lexeme);

					buf[1] = '\0';

					int w = tigrTextWidth(tfont, buf);
					int h = tigrTextHeight(tfont, buf);

					if (mode == MODE_INSERT) {
						w = 2;
					}

					tigrFill(screen, xx + x, y, w, h, tigrRGB(100, 100, 100));
				}

                x += tigrTextWidth(tfont, tokens[j].lexeme);
				drawCurX += strlen(tokens[j].lexeme);
            }

            y += tigrTextHeight(tfont, GBuffer.lines[i]);
        }

		if (curY < scrollY) {
			scrollY -= 1;
			blink = true;
		}

		if (curY >= lastMaxLine && lastMaxLine < GBuffer.numLines - 1) {
			++scrollY;
		}
		
        tigrUpdate(screen);
    }

    return 0;
}

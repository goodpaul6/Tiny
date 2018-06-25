#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include "tigr.h"

#define POS_STACK_SIZE	64
#define MAX_NUM_LINES		4096
#define MAX_TRACKED_DEFNS	128
#define MAX_LINE_LENGTH		512
#define MAX_TOKEN_LENGTH	256
#define MAX_TOKENS			512
#define WIDTH				640
#define HEIGHT				480

typedef enum
{
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL
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

typedef struct
{
	int line;
} Pos;

typedef struct
{
    bool highlight;
	
	const char* filename;

    int numLines;
    char lines[MAX_NUM_LINES][MAX_LINE_LENGTH];

	int numDefns;
	char defns[MAX_TRACKED_DEFNS][MAX_TOKEN_LENGTH];

	int posCount;
	Pos posStack[POS_STACK_SIZE];

	int inPosCount;
	Pos inPosStack[POS_STACK_SIZE];
} Buffer;

static Buffer GBuffer = { 0 };

static void Log(const char* s, ...)
{
    va_list args;
    va_start(args, s);

    vprintf(s, args);

    va_end(args);
}

static void UpdateDefinitions(void)
{
	GBuffer.numDefns = 0;

	for (int i = 0; i < GBuffer.numLines; ++i) {
		const char* s = strstr(GBuffer.lines[i], "#define");	
		if (!s) continue;

		if (GBuffer.numDefns >= MAX_TRACKED_DEFNS) break;

		s += strlen("#define");
		
		while (isspace(*s)) {
			s += 1;
		}

		int i = 0;

		while (!isspace(*s)) {
			GBuffer.defns[GBuffer.numDefns][i++] = *s++;
		}

		GBuffer.defns[GBuffer.numDefns][i] = '\0';
		GBuffer.numDefns += 1;
	}
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

	GBuffer.filename = filename;

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

	UpdateDefinitions();
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
				(strcmp(lexeme, "void") == 0) ||
				(strcmp(lexeme, "unsigned") == 0) ||
				(strcmp(lexeme, "bool") == 0) ||
				(strcmp(lexeme, "short") == 0) ||
				(strcmp(lexeme, "long") == 0) ||
				(strcmp(lexeme, "false") == 0) ||
				(strcmp(lexeme, "true") == 0) ||
				(strcmp(lexeme, "static") == 0) ||
				(strcmp(lexeme, "return") == 0)) {
				tokens[curTok].type = TOK_KEYWORD;
			}

			for (int i = 0; i < GBuffer.numDefns; ++i) {
				if (strcmp(GBuffer.defns[i], lexeme) == 0) {
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

static void RemoveLine(int y)
{
	assert(y >= 0 && y < GBuffer.numLines);

	// Shift all other lines up
	memmove(&GBuffer.lines[y], GBuffer.lines[y + 1], (GBuffer.numLines - y + 1) * sizeof(GBuffer.lines[0]));
	GBuffer.numLines -= 1;
}

static int CountBracesOnLine(int line)
{
	assert(line >= 0 && line <= GBuffer.numLines);

	const char* s = GBuffer.lines[line];

	int braces = 0;

	while (*s) {
		if (*s == '"') {
			s += 1;
			while (*s && *s != '"') ++s;
			s += 1;
		}
		else if (*s == '\'') {
			s += 2;
		}
		else if (*s == '{') {
			++braces;
			s += 1;
		}
		else if (*s == '}') {
			--braces;
			s += 1;
		}
		else {
			++s;
		}
	}

	return braces;
}

// Returns open braces - close braces upto line (upto)
static int CountBracesDown(int upto)
{
	assert(upto >= 0 && upto <= GBuffer.numLines);

	int braces = 0;

	for (int i = 0; i < upto; ++i) {
		braces += CountBracesOnLine(i);
	}

	return braces;
}

// Basically backspace (will move around yp if backspace moves up a line)
// If tabSpacing is true, it will try to jump back one tab if you backspace near a space
static void RemoveChar(int* xp, int* yp, bool tabSpacing)
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

		if (tabSpacing) {
			bool spacesBefore = true;
			
			for (int i = 0; i < x - 1; ++i) {
				if (GBuffer.lines[y][i] != ' ') {
					spacesBefore = false;
					break;
				}
			}

			if (spacesBefore) {
				static char line[MAX_LINE_LENGTH];
				strcpy(line, GBuffer.lines[y]);

				const char* s = line;
				s += strspn(s, " ");

				int spc = ((int)(x - 1) / 4);

				int pos = 0;
				for (int i = 0; i < spc * 4; ++i) {
					GBuffer.lines[y][pos++] = ' ';
				}
				GBuffer.lines[y][pos] = '\0';

				*xp = pos;
				strcat(GBuffer.lines[y], s);
			}
		}
	} else if (y > 0 && GBuffer.numLines > 1) {
		// Move the contents of the rest of this line to the end of the previous line
		// and put the cursor there

		*xp = strlen(GBuffer.lines[y - 1]);
		strcat(GBuffer.lines[y - 1], GBuffer.lines[y]);

		// Shift all other lines up
		RemoveLine(y);
		*yp -= 1;
	}
}

static void InsertChar(char ch, int* xp, int* yp)
{
	int x = *xp;
	int y = *yp;

	assert(y >= 0 && y < GBuffer.numLines);
	int len = strlen(GBuffer.lines[y]);
	assert(x >= 0 && x <= len);

	UpdateDefinitions();

	if (x == len) {
		GBuffer.lines[y][x] = ch;
		GBuffer.lines[y][x + 1] = '\0';

		if (ch == '}') {
			bool isAllSpace = true;

			for (int i = 0; i < x - 1; ++i) {
				if (!isspace(GBuffer.lines[y][i])) {
					isAllSpace = false;
					break;
				}
			}

			if (isAllSpace) {
				// We can mess with the line; it's just a close bracket
				int spc = CountBracesDown(y) - 1;
				
				int pos = 0;

				for (int i = 0; i < spc; ++i) {
					for (int k = 0; k < 4; ++k) {
						GBuffer.lines[y][pos++] = ' ';
					}
				}

				GBuffer.lines[y][pos++] = '}';
				GBuffer.lines[y][pos] = '\0';
			}
		}
	} else {
		memmove(&GBuffer.lines[y][x + 1], &GBuffer.lines[y][x], len - x);
		GBuffer.lines[y][x] = ch;
		GBuffer.lines[y][len + 1] = '\0';
	}

	*xp += 1;
}

static void InsertNewline(int* xp, int* yp, bool applySpacing)
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

	if (applySpacing) {
		// try and put the cursor at the correct place based on scope	
		// Strip all whitespace before we do that tho
		size_t span = strspn(GBuffer.lines[y], " ");

		memmove(&GBuffer.lines[y][0], &GBuffer.lines[y][span], span);

		int spc = CountBracesDown(y);

		for (int i = 0; i < spc; ++i) {
			for (int k = 0; k < 4; ++k) {
				InsertChar(' ', xp, yp);
			}
		}
	}
}

static void IndentLine(int line, int* startOfLine)
{
	const char* s = GBuffer.lines[line];
	size_t col = strspn(s, " ");
	s += col;

	static char buf[MAX_LINE_LENGTH];
	strcpy(buf, s);

	int spc = ((int)(col + 1) / 4) + 1;

	for (int i = 0; i < spc * 4; ++i) {
		GBuffer.lines[line][i] = ' ';
	}

	strcpy(GBuffer.lines[line] + spc * 4, buf);
	if(startOfLine) *startOfLine = spc * 4;
}

static void DedentLine(int line, int* startOfLine)
{
	const char* s = GBuffer.lines[line];
	size_t col = strspn(s, " ");
	s += col;

	int spc = ((int)(col - 1) / 4);

	strcpy(GBuffer.lines[line] + spc * 4, s);
	if(startOfLine) *startOfLine = spc * 4;
}

static void PushPos(int y)
{
	if (GBuffer.posCount >= POS_STACK_SIZE) {
		memmove(&GBuffer.posStack[0], &GBuffer.posStack[1], (POS_STACK_SIZE - 1) * sizeof(Pos));
		GBuffer.posCount -= 1;
	} 

	GBuffer.posStack[GBuffer.posCount++] = (Pos){ y };
}

static bool PopPos(Pos* pos, int cur)
{
	if (GBuffer.posCount == 0) return false;
	
	if (GBuffer.inPosCount >= POS_STACK_SIZE) {
		memmove(&GBuffer.inPosStack[0], &GBuffer.inPosStack[1], (POS_STACK_SIZE - 1) * sizeof(Pos));
		GBuffer.inPosCount -= 1;
	} 

	GBuffer.inPosStack[GBuffer.inPosCount++] = (Pos) { cur };

	*pos = GBuffer.posStack[--GBuffer.posCount];
	return true;
}

int main(int argc, char** argv)
{
    if(argc > 1) {
        OpenFile(argv[1]);
    }

    Tigr* screen = tigrWindow(WIDTH, HEIGHT, "Tiny Notepad", TIGR_FIXED | TIGR_2X);
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

	int scrollY = 0;
	int curX = 0, curY = 0;
	int lastMaxLine = 0;

	Mode mode = MODE_NORMAL;

	float elapsed = tigrTime();
	float lastReleaseTime[8] = { 0 };

	char cmd[3] = { 0 };

	tigrSetPostFX(screen, 0, 0, 0.5f, 1.2f);

	int startLine = -1, endLine = -1;

    while(!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));
		
        int y = 0;

		const float repeatTime = 0.6f;

		elapsed += tigrTime();
		int ticks = (int)(elapsed / 0.5f);

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

		assert(curX >= 0);

		// Movement
		if (mode == MODE_NORMAL || mode == MODE_VISUAL) {
			int val = tigrReadChar(screen);

			if (val > 0) {
				if (cmd[1] > 0) {
					cmd[0] = '\0';
				}

				char s[2] = { val, '\0' };
				strcat(cmd, s);
			}

			if (tigrKeyDown(screen, TK_ESCAPE)) {
				cmd[0] = '\0';
				mode = MODE_NORMAL;
			}

			bool left = cmd[0] == 'h';
			bool right = cmd[0] == 'l';
			bool up = cmd[0] == 'k';
			bool down = cmd[0] == 'j';

			if (left && curX > 0) {
				cmd[0] = '\0';
				--curX;
				blink = true;
			}

			if (right && curX < strlen(GBuffer.lines[curY]) - 1) {
				cmd[0] = '\0';
				++curX;
				blink = true;
			}

			if (up && curY > 0) {
				cmd[0] = '\0';
				--curY;
				blink = true;
			}

			if (down && curY < GBuffer.numLines - 2) {
				cmd[0] = '\0';
				++curY;
				blink = true;
			}

			if (cmd[0] == 15) {
				// CTRL+O
				cmd[0] = '\0';

				int y = curY;
				Pos p;

				if (PopPos(&p, curY)) {
					curY = p.line;
				}
			}

			if (cmd[0] == 9) {
				// CTRL+I
				cmd[0] = '\0';

				if (GBuffer.inPosCount > 0) {
					int y = curY;
					curY = GBuffer.inPosStack[--GBuffer.inPosCount].line;
					PushPos(y);
				}
			}

			if (cmd[0] == '{') {
				cmd[0] = '\0';

				PushPos(curY);
				if (curY > 0) curY -= 1;

				// Move up to the last empty line
				while (curY > 0 && GBuffer.lines[curY][0] != '\0') {
					curY -= 1;
				}
			}

			if (cmd[0] == '}') {
				cmd[0] = '\0';

				PushPos(curY);
				if (curY < GBuffer.numLines - 1) curY += 1;

				// Move up to the last empty line
				while (curY < GBuffer.numLines - 1 && GBuffer.lines[curY][0] != '\0') {
					curY += 1;
				}
			}

			if (mode == MODE_NORMAL) {
				if (GBuffer.numLines > 0 && cmd[0] == 'd' && cmd[1] == 'd') {
					RemoveLine(curY);
					cmd[0] = '\0';
				}

				if (cmd[0] == 'V') {
					cmd[0] = '\0';

					startLine = curY;
					endLine = curY;
					mode = MODE_VISUAL;
				}

				if (cmd[0] == 'c') {
					if (cmd[1] == 'c') {
						cmd[0] = '\0';

						RemoveLine(curY);
						int x = 0;
						InsertNewline(&x, &curY, true);
						curY -= 1;

						tigrReadChar(screen);
						mode = MODE_INSERT;
					} 			
				}

				if (cmd[0] == '<' && cmd[1] == '<') {
					cmd[0] = '\0';
					DedentLine(curY, &curX);
				}

				if (cmd[0] == '>' && cmd[1] == '>') {
					cmd[0] = '\0';
					IndentLine(curY, &curX);
				}

				if (cmd[0] == 'x') {
					cmd[0] = '\0';

					int x = curX + 1;
					if (x < strlen(GBuffer.lines[curY])) {
						RemoveChar(&x, &curY, true);
					}
					else {
						GBuffer.lines[curY][curX] = '\0';
						curX = 0;
					}
				}
			}

			if (cmd[0] == 'e') {
				cmd[0] = '\0';

				// Move to end of word
				const char* cur = &GBuffer.lines[curY][curX];

				if (cur[1] == ' ') {
					// Space right after, move up
					cur += 1;
					curX += 1;
					int spn = strspn(cur, " ");

					curX += spn;
				}
				else {
					const char* spc = strchr(cur, ' ');
					if (spc) {
						curX += spc - cur - 1;
					} else {
						curX += strlen(cur) - 1;
						if (curX < 0) curX = 0;
					}
				}
			}

			if (cmd[0] == 'b') {
				cmd[0] = '\0';

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
				} else {
					while (curX > 0 && cur[-1] != ' ') {
						--curX;
						--cur;
					}
				}

				if (curX < 0) curX = 0;
			} else if(mode == MODE_VISUAL) {
                int a = startLine;
                int b = endLine;

                if (a > b) {
                    int temp = b;
                    b = a;
                    a = temp;
                }

				if (cmd[0] == 'd' || cmd[0] == 'c') {
					for (int i = a; i <= b; ++i) {
						RemoveLine(a);
					}

					curY = a;

					if (curY >= GBuffer.numLines) curY = GBuffer.numLines - 1;
					if (curY < 0) curY = 0;

					mode = cmd[0] == 'd' ? MODE_NORMAL : MODE_INSERT;

					cmd[0] = '\0';
				} 

				if (cmd[0] == '>') {
					cmd[0] = '\0';

					int start = 0;
					int* pStart = &start;

                    for(int i = a; i <= b; ++i) {
                        IndentLine(i, pStart);
						pStart = NULL;
                    }

					curX = start;
					curY = a;
					
					mode = MODE_NORMAL;
				}

                if(cmd[0] == '<') {
					cmd[0] = '\0';

					int start = 0;
					int* pStart = &start;

                    for(int i = a; i <= b; ++i) {
                        DedentLine(i, pStart);
						pStart = NULL;
                    }

					curX = start;
					curY = a;

					mode = MODE_NORMAL;
                }
			}

			if (cmd[0] == '%') {
				cmd[0] = '\0';

				if (GBuffer.lines[curY][curX] == '}') {
					int balance = 0;
					
					int line = -1;

					for (int i = curY; i >= 0; --i) {
						balance += CountBracesOnLine(i);
						if (balance == 0) {
							line = i;
							break;
						}
					}

					if (line >= 0) {
						// TODO(Apaar): Find the brace properly
						PushPos(curY);
						curY = line;
						curX = strrchr(GBuffer.lines[curY], '{') - GBuffer.lines[curY];
					}
				} else if (GBuffer.lines[curY][curX] == '{') {
					int balance = 0;
					
					int line = -1;

					for (int i = curY; i < GBuffer.numLines; ++i) {
						balance += CountBracesOnLine(i);
						if (balance == 0) {
							line = i;
							break;
						}
					}

					if (line >= 0) {
						// TODO(Apaar): Find the brace properly
						PushPos(curY);
						curY = line;
						curX = strrchr(GBuffer.lines[curY], '}') - GBuffer.lines[curY];
					}
				}
			}

			if (cmd[0] == 'i' || cmd[0] == 'I') {
				if (cmd[0] == 'I') {
					curX = 0;
				}

				cmd[0] = '\0';

				mode = MODE_INSERT;
			}

			if (cmd[0] == 'a' || cmd[0] == 'A') {
				if (cmd[0] == 'A') {
					curX = strlen(GBuffer.lines[curY]);
				} else {
					curX += 1;
				}

				cmd[0] = '\0';

				mode = MODE_INSERT;
			}

			if (cmd[0] == 'o' || cmd[0] == 'O') {
				if (cmd[0] == 'O') {
					curX = 0;
					InsertNewline(&curX, &curY, false);
					curY -= 1;

					int spc = CountBracesDown(curY);
					for (int i = 0; i < spc * 4; ++i) {
						InsertChar(' ', &curX, &curY);
					}
				} else {
					curX = strlen(GBuffer.lines[curY]);
					InsertNewline(&curX, &curY, true);
				}

				cmd[0] = '\0';

				mode = MODE_INSERT;
			}

			if (cmd[0] == 'g' || cmd[0] == 'G') {
				if (cmd[0] == 'G') {
					cmd[0] = '\0';

					PushPos(curY);

					scrollY = GBuffer.numLines - (lastMaxLine - scrollY) - 1;
					curY = GBuffer.numLines - 2;
				} else if(cmd[1] == 'g') {
					cmd[0] = '\0';

					PushPos(curY);

					scrollY = 0;
					curY = 0;
				}	
			}

			if (cmd[0] == 'c' && cmd[1] == 'e') {
				cmd[0] = '\0';

				// Move to end of word
				const char* cur = &GBuffer.lines[curY][curX];

				if (cur[1] == ' ') {
					// Space right after, move up
					RemoveChar(&curX, &curY, false);
					curX += 1;
					int spn = strspn(cur, " ");

					for (int i = 0; i < spn; ++i) {
						RemoveChar(&curX, &curY, false);
						curX += 1;
					}
				} else {
					const char* spc = strchr(cur, ' ');
					if (spc) {
						for (int i = 0; i < spc - cur; ++i) {
							curX += 1;
							RemoveChar(&curX, &curY, false);
						}
					} else {
						GBuffer.lines[curY][curX] = '\0';
					}
				}

				mode = MODE_INSERT;
			}

			if (mode == MODE_VISUAL) {
				endLine = curY;
			}
		} else if(mode == MODE_INSERT) {
			if (tigrKeyDown(screen, TK_ESCAPE)) {
				tigrReadChar(screen);
				cmd[0] = '\0';
				mode = MODE_NORMAL;
			} else {
				int value = tigrReadChar(screen);
				
				if (value == '\b') {
					RemoveChar(&curX, &curY, true);
				} else if (value == 127) {
					// CTRL+BACKSPACE

					const char* cur = &GBuffer.lines[curY][curX];

					if (curX > 0 && cur[-1] == ' ') {
						// Space right before, move back
						RemoveChar(&curX, &curY, false);

						while (*cur == ' ') {
							RemoveChar(&curX, &curY, false);
							cur--;
						}

						while (curX > 0 && *cur != ' ') {
							RemoveChar(&curX, &curY, false);
							cur--;
						}
					} else {
						while (curX > 0 && cur[-1] != ' ') {
							RemoveChar(&curX, &curY, false);
							--cur;
						}
					}

					if (curX < 0) curX = 0;
				} else if (value == '\n') {
					InsertNewline(&curX, &curY, true);
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
		}

        for(int i = scrollY; i < GBuffer.numLines; ++i) {
			if (y >= HEIGHT) {
				lastMaxLine = i;
				break;
			}

			int drawCurX = 0;
            int x = 0;

#if 1
			// Show line numbers
			char lbuf[32];
			sprintf(lbuf, "%*d", (int)ceil(log10(GBuffer.numLines)), i + 1);

			tigrPrint(screen, font, x, y, tigrRGB(40, 40, 40), lbuf);
			x += tigrTextWidth(font, lbuf) + 4;
#endif

			if (mode == MODE_VISUAL) {
				int a = startLine;
				int b = endLine;

				if (a > b) {
					int temp = b;
					b = a;
					a = temp;
				}
				
				if (i >= a && i <= b) {
					tigrFill(screen, x, y, tigrTextWidth(font, GBuffer.lines[i]), tigrTextHeight(font, GBuffer.lines[i]), tigrRGB(80, 80, 80));
				}
			}

			int lineLen = strlen(GBuffer.lines[i]);

			if (i == curY) {
				if (mode == MODE_NORMAL) {
					if (curX >= lineLen) {
						// Move back to bounds of line
						curX = lineLen - 1;
						if (curX < 0) curX = 0;
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

					int w = tigrTextWidth(font, " ");
					int h = tigrTextHeight(font, "A");

					if (mode == MODE_INSERT) {
						w = 2;
					}

					tigrFill(screen, x, y, w, h, tigrRGB(100, 100, 100));
				} else if(curX == lineLen) {
					assert(mode != MODE_NORMAL);

					int w = tigrTextWidth(font, GBuffer.lines[curY]);
					int h = tigrTextHeight(font, GBuffer.lines[curY]);

					tigrFill(screen, x + w, y, 2, h, tigrRGB(100, 100, 100));
				}
			}

            for (int j = 0; j < numTokens; ++j) {
				int len = strlen(tokens[j].lexeme);

                tigrPrint(screen, font, x, y, tokenColors[tokens[j].type], tokens[j].lexeme);

				if (blink && i == curY && drawCurX <= curX && curX < drawCurX + len) {
					char buf[MAX_TOKEN_LENGTH];
					strcpy(buf, tokens[j].lexeme);

					buf[curX - drawCurX] = '\0';

					int xx = tigrTextWidth(font, buf);

					strcpy(buf, tokens[j].lexeme);

					buf[1] = '\0';

					int w = tigrTextWidth(font, buf);
					int h = tigrTextHeight(font, buf);

					if (mode == MODE_INSERT) {
						w = 2;
					}

					tigrFill(screen, xx + x, y, w, h, tigrRGB(100, 100, 100));
				}

                x += tigrTextWidth(font, tokens[j].lexeme);
				drawCurX += strlen(tokens[j].lexeme);
            }

            y += tigrTextHeight(font, GBuffer.lines[i]);
        }

		if (curY < scrollY) {
			scrollY = curY;
			blink = true;
		}

		if (curY >= lastMaxLine && lastMaxLine < GBuffer.numLines - 1) {
			scrollY = curY - (lastMaxLine - scrollY) + 1;
		}
		
        tigrUpdate(screen);
    }

	tigrFree(screen);

    return 0;
}

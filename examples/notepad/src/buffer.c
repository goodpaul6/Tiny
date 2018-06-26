#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "display.h"
#include "buffer.h"

static void UpdateDefinitions(Buffer* buf)
{
	buf->numDefns = 0;

	for (int i = 0; i < buf->numLines; ++i) {
		const char* s = strstr(buf->lines[i], "#define");	
		if (!s) continue;

		if (buf->numDefns >= MAX_TRACKED_DEFNS) break;

		s += strlen("#define");
		
		while (isspace(*s)) {
			s += 1;
		}

		int i = 0;

		while (!isspace(*s)) {
			buf->defns[buf->numDefns][i++] = *s++;
		}

		buf->defns[buf->numDefns][i] = '\0';
		buf->numDefns += 1;
	}
}

void InitDefaultBuffer(Buffer* buf)
{
    buf->filetype = FILE_UNKNOWN;

    buf->numLines = 2;
    strcpy(buf->lines[0], "Welcome to Tiny Notepad!");

    buf->numDefns = 0;
}

void OpenFile(Buffer* buf, const char* filename)
{
    sprintf(Status, "Opening file '%s'.", filename);

    FILE* f = fopen(filename, "r");

    if(!f) {
        sprintf(Status, "Failed to open file '%s' for reading.", filename);
        return;
    }

    int last = getc(f);
	
	const char* ext = strrchr(filename, '.');
	if (ext) {
        if(strcmp(ext, ".c") == 0 || strcmp(ext, ".cc") == 0 || strcmp(ext, ".h") == 0 || 
                strcmp(ext, ".hh") == 0 || strcmp(ext, ".hpp") == 0) {
            buf->filetype = FILE_C;
        } else if(strcmp(ext, ".tiny") == 0) {
            buf->filetype = FILE_TINY;
        }
	} else {
        buf->filetype = FILE_UNKNOWN;
    }

    int curChar = 0;
    int curLine = 0;

    while (last != EOF) {
        if (last == '\n') {
            buf->lines[curLine][curChar] = '\0';
            curLine += 1;
            curChar = 0;
        } else {
            if (curLine >= MAX_NUM_LINES) {
                printf("File exceeded maximum number of lines. Only partially loaded.");

                buf->numLines = curLine;
                fclose(f);
                return;
            }

            if (curChar >= MAX_LINE_LENGTH - 1) {
                continue;
            }

			if (last == '\t') {
				// Translate tabs to 4 spaces
				for (int i = 0; i < 4; ++i) {
					buf->lines[curLine][curChar++] = ' ';
				}
			} else {
				buf->lines[curLine][curChar++] = last;
			}
        }

        last = getc(f);
    }

	buf->numLines = curLine + 1;

    fclose(f);

	UpdateDefinitions(buf);
}

const char* GetLine(Buffer* buf, int y)
{
    assert(y >= 0 && y < buf->numLines);
    return buf->lines[y];
}

void SetLine(Buffer* buf, int y, const char* text)
{
    assert(y >= 0 && y < buf->numLines);
    strcpy(buf->lines[y], text);
}

void InsertEmptyLine(Buffer* buf, int y)
{
    assert(buf->numLines + 1 < MAX_NUM_LINES);

    // Shift rest of the lines down
    memmove(&buf->lines[y + 1], &buf->lines[y], (buf->numLines - y) * MAX_LINE_LENGTH);

    // Empty line
    buf->lines[y][0] = 0;
	buf->numLines += 1;
}

void RemoveLine(Buffer* buf, int y)
{
    assert(y >= 0 && y < buf->numLines);

    memmove(&buf->lines[y], &buf->lines[y + 1], (buf->numLines - y + 1) * MAX_LINE_LENGTH);
    buf->numLines -= 1;
}

void InsertChar(Buffer* buf, int x, int y, char ch)
{
    assert(y >= 0 && y < buf->numLines);

    int len = strlen(buf->lines[y]);

    assert(x >= 0 && x <= len);
    assert(len < MAX_LINE_LENGTH);
    
    if(x == len) {
        // End of line
        buf->lines[y][x] = ch;
        buf->lines[y][x + 1] = 0;
    } else {
        // Shift chars over
        memmove(&buf->lines[y][x + 1], &buf->lines[y][x], len - x);
        // Put this in the hole
        buf->lines[y][x] = ch;
        buf->lines[y][len + 1] = '\0';
    }

    UpdateDefinitions(buf);
}

void InsertString(Buffer* buf, int x, int y, const char* str)
{
    assert(y >= 0 && y < buf->numLines);

    int len = strlen(buf->lines[y]);
    int slen = strlen(str);

    assert(x >= 0 && x <= len);
    assert(slen + len < MAX_LINE_LENGTH - 1);

    if(x == len) {
        strcat(buf->lines[y], str);
    } else {
        memmove(&buf->lines[y][x + slen], &buf->lines[y][x], len - x + slen); 
        memcpy(&buf->lines[y][x], str, slen);

        buf->lines[y][len + slen + 1] = '\0';
    }

    UpdateDefinitions(buf);
}

void RemoveChar(Buffer* buf, int x, int y)
{ 
    assert(y >= 0 && y < buf->numLines);

    int len = strlen(buf->lines[y]);

    assert(x >= 0 && x < len);

    if(x == len - 1) {
        buf->lines[y][x] = '\0';
    } else {
        memmove(&buf->lines[y][x], &buf->lines[y][x + 1], len - x - 1); 
        buf->lines[y][len - 1] = '\0';
    }
}

// Puts a null terminator at pos
void TerminateLine(Buffer* buf, int x, int y)
{
    assert(y >= 0 && y < buf->numLines);

    int len = strlen(buf->lines[y]);

    assert(x >= 0 && x <= len);

    buf->lines[y][x] = '\0';
}

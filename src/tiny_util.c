#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tiny_util.h"

char* Tiny_Strdup(const char* s)
{
    char* ss = malloc(strlen(s) + 1);
    strcpy(ss, s);
    return ss;
}

int Tiny_TranslatePosToLineNumber(const char* src, Tiny_TokenPos pos)
{
    int curPos = 0;
    int lineNumber = 1;

    while(curPos < pos) {
        assert(src[curPos]);

        if(src[curPos] == '\n') {
            ++lineNumber;
        }

        curPos += 1;
    }

    return lineNumber;
}

void Tiny_ReportErrorV(const char* fileName, const char* src, Tiny_TokenPos pos, const char* s, va_list args)
{
	fputc('\n', stderr);

    if(src) {
        int lineNumber = Tiny_TranslatePosToLineNumber(src, pos);
        int curLine = 1;

        // Print nearest 3 lines
        while(*src) {
            if(abs(lineNumber - curLine) <= 3) {
                if(curLine == lineNumber) {
                    fprintf(stderr, "%d ->\t", curLine);
                } else {
                    fprintf(stderr, "%d\t", curLine);
                }

                while(*src && *src != '\n') {
                    fputc(*src, stderr);
                    src += 1;
                }

				if (*src == '\n') {
					src += 1;
					fputc('\n', stderr);
				}

                curLine += 1;
            } else {
				if (*src == '\n') {
					++curLine;
				}
                src += 1;
            }     
        }

        fputc('\n', stderr);

        fprintf(stderr, "ERROR %s(%d): ", fileName, lineNumber);
        vfprintf(stderr, s, args);
        fputc('\n', stderr);
    } else {
        fprintf(stderr, "ERROR: ");
        vfprintf(stderr, s, args);
        fputc('\n', stderr);
    }
}

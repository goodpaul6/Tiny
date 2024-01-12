#include "util.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tiny.h"

void *TMalloc(Tiny_Context *ctx, size_t size) { return ctx->alloc(NULL, size, ctx->userdata); }

void *TRealloc(Tiny_Context *ctx, void *ptr, size_t size) {
    return ctx->alloc(ptr, size, ctx->userdata);
}

void TFree(Tiny_Context *ctx, void *ptr) { ctx->alloc(ptr, 0, ctx->userdata); }

int Tiny_TranslatePosToLineNumber(const char *src, Tiny_TokenPos pos) {
    int curPos = 0;
    int lineNumber = 1;

    while (curPos < pos) {
        assert(src[curPos]);

        if (src[curPos] == '\n') {
            ++lineNumber;
        }

        curPos += 1;
    }

    return lineNumber;
}

void Tiny_FormatErrorV(char *buf, size_t bufsize, const char *fileName, const char *src,

                       Tiny_TokenPos pos, const char *s, va_list args) {
    assert(buf);

    size_t used = 0;

#define APPEND(s, ...)                                              \
    do {                                                            \
        int64_t rembufsize = (int64_t)bufsize - used;               \
        if (rembufsize < 0) {                                       \
            rembufsize = 0;                                         \
        }                                                           \
        used += snprintf(buf + used, rembufsize, s, ##__VA_ARGS__); \
    } while (0)

#define APPEND_V(s, vargs)                                   \
    do {                                                     \
        int64_t rembufsize = (int64_t)bufsize - used;        \
        if (rembufsize < 0) {                                \
            rembufsize = 0;                                  \
        }                                                    \
        used += vsnprintf(buf + used, rembufsize, s, vargs); \
    } while (0)

    APPEND("\n");

    if (src) {
        int lineNumber = Tiny_TranslatePosToLineNumber(src, pos);
        int curLine = 1;

        // Print nearest 3 lines
        while (*src) {
            if (abs(lineNumber - curLine) <= 3) {
                if (curLine == lineNumber) {
                    APPEND("%d ->\t", curLine);
                } else {
                    APPEND("%d\t", curLine);
                }

                while (*src && *src != '\n') {
                    APPEND("%c", *src);
                    src += 1;
                }

                if (*src == '\n') {
                    src += 1;
                    APPEND("\n");
                }

                curLine += 1;
            } else {
                if (*src == '\n') {
                    ++curLine;
                }
                src += 1;
            }
        }

        APPEND("\n");

        APPEND("ERROR %s(%d): ", fileName, lineNumber);
        APPEND_V(s, args);
        APPEND("\n");
    } else {
        APPEND("ERROR: ");
        APPEND_V(s, args);
        APPEND("\n");
    }
}

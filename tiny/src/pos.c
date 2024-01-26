#ifndef TINY_NO_COMPILER

#include <assert.h>
#include <string.h>

#include "pos.h"

Tiny_FriendlyPos Tiny_PosToFriendlyPos(Tiny_Pos pos, const char *src, uint32_t srcLen) {
    assert(src);
    assert(pos.index < srcLen);

    Tiny_FriendlyPos friendlyPos = {0};

    const char *start = src;

    for (;;) {
        const char *lineStart = src;
        const char *newLine = strchr(lineStart, '\n');

        // We have crossed the pos
        if (!newLine || newLine - start >= pos.index) {
            const char *cursor = lineStart;

            assert(cursor - start <= pos.index);

            // Move up to the pos char-by-char
            while (cursor - start < pos.index) {
                ++cursor;
            }

            friendlyPos.charIndex = cursor - lineStart;
            break;
        }

        ++friendlyPos.lineIndex;
        src = newLine + 1;
    }

    return friendlyPos;
}

#endif

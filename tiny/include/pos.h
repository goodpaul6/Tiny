#ifndef POS_H
#define POS_H

#include <stdint.h>

// Used to refer to a location in the source code.
typedef struct Tiny_Pos {
    uint32_t index;
} Tiny_Pos;

// Zero-indexed line and char number.
typedef struct Tiny_FriendlyPos {
    uint32_t lineIndex;
    uint32_t charIndex;
} Tiny_FriendlyPos;

// Convert a Tiny_Pos to a line number
Tiny_FriendlyPos Tiny_PosToFriendlyPos(Tiny_Pos pos, const char *src, uint32_t srcLen);

#endif

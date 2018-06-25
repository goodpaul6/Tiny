#pragma once

#include "tiny.h"
#include "buffer.h"

typedef struct Tigr Tigr;

typedef enum
{
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL_LINE
} Mode;

typedef struct
{
    // y - line
    // x - character
    int y, x;
} Pos;

typedef struct Editor
{
    Mode mode;
    Pos cur;
    
    // Visual mode selection start
    Pos vStart;

    // Line to start rendering at
    int scrollY;

    Buffer buf;

    bool blink;

    float elapsed;

    // We hold onto this screen for input
    Tigr* screen;

    // Main functionality of the editor provided by this script
    Tiny_State* state;
    Tiny_StateThread thread;

    int updateFunction;
} Editor;

void InitEditor(Editor* ed);

void FileOpened(Editor* ed, const char* name);

void UpdateEditor(Editor* ed, Tigr* screen);

void DestroyEditor(Editor* ed);

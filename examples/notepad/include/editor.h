#pragma once

#define MAX_COMMAND_LENGTH  512
#define MAX_FILENAME_LENGTH 256
#define MAX_TEMP_LINES      1024

#include "tiny.h"
#include "buffer.h"

typedef struct Tigr Tigr;

typedef enum
{
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL_LINE,
    MODE_COMMAND,
	MODE_FORWARD_SEARCH
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

    // Can be used by the script to store strings
    int numTempLines;
    char tempLines[MAX_TEMP_LINES][MAX_LINE_LENGTH];

    float blinkTime;

    float elapsed;

    // When we enter command/search mode, all read characters are put into this buffer
    char cmd[MAX_COMMAND_LENGTH];

    // Whenever file is opened, this is set and can be retrieved by script
    char filename[MAX_FILENAME_LENGTH];

    // We hold onto this screen for input
    Tigr* screen;

    // Main functionality of the editor provided by this script
    Tiny_State* state;
    Tiny_StateThread thread;

    int updateFunction;

    // If this is set, scripts will be reloaded
    bool shouldReloadScripts;
} Editor;

void InitEditor(Editor* ed);

void FileOpened(Editor* ed, const char* name);

void UpdateEditor(Editor* ed, Tigr* screen);

void DestroyEditor(Editor* ed);

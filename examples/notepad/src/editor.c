#include <assert.h>
#include <string.h>

#include "tigr.h"
#include "display.h"
#include "editor.h"

static float StatusTime = 0;

static Tiny_Value Lib_SetStatus(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
    strcpy(Status, Tiny_ToString(args[0]));

    return Tiny_Null;
}

static Tiny_Value Lib_LineCount(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    return Tiny_NewNumber((double)ed->buf.numLines);
}

static Tiny_Value Lib_GetLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 1) {
        int index = (int)Tiny_ToNumber(args[0]);

        return Tiny_NewConstString(GetLine(&ed->buf, index));
    } else {
        assert(count == 0);

        return Tiny_NewConstString(GetLine(&ed->buf, ed->cur.y));
    }
}

static Tiny_Value Lib_SetLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 2) {
        int index = (int)Tiny_ToNumber(args[0]);
        const char* s = Tiny_ToString(args[1]);

        SetLine(&ed->buf, index, s);
    } else {
        assert(count == 1);
        const char* s = Tiny_ToString(args[0]);

        SetLine(&ed->buf, ed->cur.y, s);
    }

    return Tiny_Null;
}

static Tiny_Value Lib_GetX(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;
    return Tiny_NewNumber((double)ed->cur.x);
}

static Tiny_Value Lib_GetY(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;
    return Tiny_NewNumber((double)ed->cur.y);
}

static Tiny_Value Lib_Move(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 2);

    int x = (int)Tiny_ToNumber(args[0]);
    int y = (int)Tiny_ToNumber(args[1]);

    ed->cur.x += x;
    ed->cur.y += y;

    if(ed->cur.y < 0) ed->cur.y = 0;
    if(ed->cur.y >= ed->buf.numLines) ed->cur.y = ed->buf.numLines - 1;

    if(ed->cur.x < 0) ed->cur.x = 0;

    int len = strlen(GetLine(&ed->buf, ed->cur.y));

    if(ed->mode == MODE_INSERT) {
        // In insert mode, it's fine if you're past the end of a line
        if(ed->cur.x > len) ed->cur.x = len;
    } else {
        if(ed->cur.x >= len) ed->cur.x = len - 1;
		// 0 is an exception
		if (ed->cur.x < 0) ed->cur.x = 0;
    }

    // Make sure the cursor is on the page
    if(ed->cur.y < ed->scrollY) {
        ed->scrollY = ed->cur.y;
    }

    if(ed->cur.y >= ed->scrollY + LINES_PER_PAGE) {
		ed->scrollY = ed->cur.y - LINES_PER_PAGE + 1;
    }

    return Tiny_Null;
}

static Tiny_Value Lib_ReadChar(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 0);
    assert(ed->screen);

    return Tiny_NewNumber((double)tigrReadChar(ed->screen));
}

static void BindFunctions(Tiny_State* state)
{
    Tiny_BindFunction(state, "set_status", Lib_SetStatus);
    Tiny_BindFunction(state, "line_count", Lib_LineCount);
    Tiny_BindFunction(state, "get_line", Lib_GetLine);
    Tiny_BindFunction(state, "set_line", Lib_SetLine);
    Tiny_BindFunction(state, "get_x", Lib_GetX);
    Tiny_BindFunction(state, "get_y", Lib_GetY);
    Tiny_BindFunction(state, "move", Lib_Move);
    Tiny_BindFunction(state, "read_char", Lib_ReadChar);
}

void InitEditor(Editor* ed)
{
    ed->mode = MODE_NORMAL;

    ed->cur.x = ed->cur.y = 0;

    ed->blink = false;

    ed->scrollY = 0;

    ed->elapsed = tigrTime();

    InitDefaultBuffer(&ed->buf);

    // Setup script
    ed->state = Tiny_CreateState();

    BindFunctions(ed->state);

    Tiny_CompileFile(ed->state, "scripts/main.tiny");

    Tiny_InitThread(&ed->thread, ed->state);
    Tiny_StartThread(&ed->thread);

    ed->thread.userdata = ed;

    while(Tiny_ExecuteCycle(&ed->thread));

    ed->updateFunction = Tiny_GetFunctionIndex(ed->state, "update");

    assert(ed->updateFunction >= 0);
}

void FileOpened(Editor* ed, const char* name)
{
    int fileOpened = Tiny_GetFunctionIndex(ed->state, "fileOpened");

    if(fileOpened >= 0) {
        Tiny_Value args[1] = {
            Tiny_NewConstString(name)
        };

        Tiny_CallFunction(&ed->thread, fileOpened, args, 1);
    }
}

void UpdateEditor(Editor* ed, Tigr* screen)
{
    ed->screen = screen;

    float diff = tigrTime();

    ed->elapsed += diff;

    if(Status[0]) {
        StatusTime += diff;

        if(StatusTime > 3.0f) {
            Status[0] = 0;
            StatusTime = 0;
        }
    }
    
    int ticks = (int)(ed->elapsed / 0.5f);
    
    ed->blink = ticks % 2 == 0;

    Tiny_CallFunction(&ed->thread, ed->updateFunction, NULL, 0);
}

void DestroyEditor(Editor* ed)
{
    Tiny_DestroyThread(&ed->thread);
    Tiny_DeleteState(ed->state);
}

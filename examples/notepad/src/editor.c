#include <assert.h>
#include <string.h>
#include <math.h>

#include "tigr.h"
#include "display.h"
#include "editor.h"

static float StatusTime = 0;

static void MoveTo(Editor* ed, int x, int y)
{
    ed->cur.x = x;
    ed->cur.y = y;

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
}

static Tiny_Value Lib_Strlen(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
    return Tiny_NewNumber((double)strlen(Tiny_ToString(args[0])));
}

static Tiny_Value Lib_Strspn(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);
    return Tiny_NewNumber((double)strspn(Tiny_ToString(args[0]), Tiny_ToString(args[1])));
}

static Tiny_Value Lib_Stridx(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);
    return Tiny_NewNumber((double)Tiny_ToString(args[0])[(int)Tiny_ToNumber(args[1])]);
}

static Tiny_Value Lib_Floor(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
	return Tiny_NewNumber((double)floor(Tiny_ToNumber(args[0])));
}

static Tiny_Value Lib_Ceil(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
	return Tiny_NewNumber((double)ceil(Tiny_ToNumber(args[0])));
}

static Tiny_Value Lib_SetStatus(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    const char* fmt = Tiny_ToString(args[0]);
    char* s = Status;

    int arg = 1;

    while(*fmt) {
        if(*fmt == '%') {
            assert(arg < count);

            fmt += 1;
            switch(*fmt) {
                case 's': s += sprintf(s, "%s", Tiny_ToString(args[arg])); break;
                case 'g': s += sprintf(s, "%g", Tiny_ToNumber(args[arg])); break;
                case 'c': s += sprintf(s, "%c", (char)Tiny_ToNumber(args[arg])); break;
            }
            fmt += 1;

            arg += 1;
        } else {
            *s++ = *fmt++;
        }
    }

    *s = 0;

    return Tiny_Null;
}

static Tiny_Value Lib_GetVstartX(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Editor* ed = thread->userdata;
	assert(ed->mode == MODE_VISUAL_LINE);

	return Tiny_NewNumber((double)ed->vStart.x);
}

static Tiny_Value Lib_GetVstartY(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Editor* ed = thread->userdata;
	assert(ed->mode == MODE_VISUAL_LINE);

	return Tiny_NewNumber((double)ed->vStart.y);
}

static Tiny_Value Lib_GetMode(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;
	return Tiny_NewNumber((double)ed->mode);
}

static Tiny_Value Lib_SetMode(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	assert(count == 1);

    Editor* ed = thread->userdata;

	ed->mode = (int)Tiny_ToNumber(args[0]);

	if (ed->mode == MODE_VISUAL_LINE) {
		ed->vStart = ed->cur;
	}

	MoveTo(ed, ed->cur.x, ed->cur.y);

	return Tiny_Null;
}

static Tiny_Value Lib_SetChar(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 1);

    ed->buf.lines[ed->cur.y][ed->cur.x] = (char)Tiny_ToNumber(args[0]);

    return Tiny_Null;
}

static Tiny_Value Lib_RemoveChar(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Editor* ed = thread->userdata;

	assert(count == 0);

	RemoveChar(&ed->buf, ed->cur.x, ed->cur.y);
	return Tiny_Null;
}

static Tiny_Value Lib_InsertChar(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 1);

	InsertChar(&ed->buf, ed->cur.x, ed->cur.y, (char)Tiny_ToNumber(args[0]));

	return Tiny_Null;
}

static Tiny_Value Lib_InsertString(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 1);

	InsertString(&ed->buf, ed->cur.x, ed->cur.y, Tiny_ToString(args[0]));

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

static Tiny_Value Lib_GetLineFrom(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 2) {
        int start = (int)Tiny_ToNumber(args[0]);
        int index = (int)Tiny_ToNumber(args[1]);

        return Tiny_NewConstString(GetLine(&ed->buf, index) + start);
    } else {
        assert(count == 1);
        int start = (int)Tiny_ToNumber(args[0]);

        return Tiny_NewConstString(GetLine(&ed->buf, ed->cur.y) + start);
    }
}

static Tiny_Value Lib_TerminateLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 2) {
        int index = (int)Tiny_ToNumber(args[0]);
        int pos = (int)Tiny_ToNumber(args[1]);

        TerminateLine(&ed->buf, pos, index);
    } else {
        assert(count == 1);
        int pos = (int)Tiny_ToNumber(args[0]);

        TerminateLine(&ed->buf, pos, ed->cur.y);
    }

    return Tiny_Null;
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

static Tiny_Value Lib_RemoveLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 1) {
        int index = (int)Tiny_ToNumber(args[0]);

        RemoveLine(&ed->buf, index);
    } else {
        assert(count == 0);
        RemoveLine(&ed->buf, ed->cur.y);
    }

    return Tiny_Null;
}

static Tiny_Value Lib_InsertEmptyLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    if(count == 1) {
        int index = (int)Tiny_ToNumber(args[0]);

        InsertEmptyLine(&ed->buf, index);
    } else {
        assert(count == 0);
        InsertEmptyLine(&ed->buf, ed->cur.y);
		ed->cur.x = 0;
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

static Tiny_Value Lib_MoveTo(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Editor* ed = thread->userdata;

	assert(count == 2);

    int x = (int)Tiny_ToNumber(args[0]);
    int y = (int)Tiny_ToNumber(args[1]);

	MoveTo(ed, x, y);

    return Tiny_Null;
}

static Tiny_Value Lib_Move(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 2);

    int x = (int)Tiny_ToNumber(args[0]);
    int y = (int)Tiny_ToNumber(args[1]);

	MoveTo(ed, ed->cur.x + x, ed->cur.y + y);

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
	Tiny_BindConstNumber(state, "MODE_INSERT", (double)MODE_INSERT);
	Tiny_BindConstNumber(state, "MODE_NORMAL", (double)MODE_NORMAL);
	Tiny_BindConstNumber(state, "MODE_VISUAL_LINE", (double)MODE_VISUAL_LINE);

    Tiny_BindFunction(state, "strlen", Lib_Strlen);
    Tiny_BindFunction(state, "strspn", Lib_Strspn);
    Tiny_BindFunction(state, "stridx", Lib_Stridx);

    Tiny_BindFunction(state, "floor", Lib_Floor);
    Tiny_BindFunction(state, "ceil", Lib_Ceil);

    Tiny_BindFunction(state, "set_status", Lib_SetStatus);
	Tiny_BindFunction(state, "get_vstart_x", Lib_GetVstartX);
	Tiny_BindFunction(state, "get_vstart_y", Lib_GetVstartY);
    Tiny_BindFunction(state, "get_mode", Lib_GetMode);
    Tiny_BindFunction(state, "set_mode", Lib_SetMode);
    Tiny_BindFunction(state, "set_char", Lib_SetChar);
    Tiny_BindFunction(state, "remove_char", Lib_RemoveChar);
    Tiny_BindFunction(state, "insert_char", Lib_InsertChar);
    Tiny_BindFunction(state, "insert_string", Lib_InsertString);
    Tiny_BindFunction(state, "line_count", Lib_LineCount);
    Tiny_BindFunction(state, "get_line", Lib_GetLine);
    Tiny_BindFunction(state, "get_line_from", Lib_GetLineFrom);
    Tiny_BindFunction(state, "set_line", Lib_SetLine);
    Tiny_BindFunction(state, "remove_line", Lib_RemoveLine);
    Tiny_BindFunction(state, "insert_empty_line", Lib_InsertEmptyLine);
    Tiny_BindFunction(state, "terminate_line", Lib_TerminateLine);
    Tiny_BindFunction(state, "get_x", Lib_GetX);
    Tiny_BindFunction(state, "get_y", Lib_GetY);
    Tiny_BindFunction(state, "move_to", Lib_MoveTo);
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
    int fileOpened = Tiny_GetFunctionIndex(ed->state, "file_opened");

    if(fileOpened >= 0) {
        Tiny_Value args[1] = {
            Tiny_NewString(&ed->thread, estrdup(name))
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

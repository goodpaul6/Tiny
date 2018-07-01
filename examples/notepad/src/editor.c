#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

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

    if(ed->cur.y >= ed->scrollY + LINES_PER_PAGE - 1) {
        ed->scrollY = ed->cur.y - LINES_PER_PAGE + 1;
    }

    ed->blinkTime = 0;
}

static Tiny_Value Lib_ReloadScriptsNextFrame(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;
    ed->shouldReloadScripts = true;

    return Tiny_Null;
}

static Tiny_Value Lib_Exit(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    exit((int)Tiny_ToNumber(args[0]));
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

static Tiny_Value Lib_Substr(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 3);

    const char* s = Tiny_ToString(args[0]);
    int start = (int)Tiny_ToNumber(args[1]);
    int end = (int)Tiny_ToNumber(args[2]);

    if(end == -1) {
        end = strlen(s);
    }

    assert(start >= 0 && start <= end);

    if(start == end) {
        return Tiny_NewConstString("");
    }

    assert(end <= strlen(s));

    char* sub = malloc(end - start + 1);
    for(int i = start; i < end; ++i) {
        sub[i - start] = s[i];
    }

    sub[end - start] = '\0';
    
    // TODO(Apaar): Figure out if there's a way we can just not allocate for this
    return Tiny_NewString(thread, sub);
}

static Tiny_Value Lib_Strtod(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    const char* s = Tiny_ToString(args[0]);

    assert(s);

    return Tiny_NewNumber((double)strtod(s, NULL));
}

static bool MoveToFirstOccuranceOfString(Editor* ed, const char* s, bool searchUp)
{
    bool found = false;
    int y = ed->cur.y;

    while(!found) {
        int end = ed->buf.numLines;
        int wrapStart = 0;
        int move = 1;

        if(searchUp) {
            end = 0;
            move = -1;
            wrapStart = ed->buf.numLines;
        }

        for(int i = y; i != end; i += move) {
            const char* line = ed->buf.lines[i];

            if(i == ed->cur.y && y != wrapStart) {
                line += ed->cur.x;
            }

            const char* s = strstr(line, ed->cmd);
            if(s) {
                MoveTo(ed, s - ed->buf.lines[i], i);
                return true;
            }
        }

        if(y == wrapStart) break;
        y = wrapStart;

        if(!found) {
            StatusTime = 0;
            strcpy(Status, "Search wrapped around.");
        }
    }

    return found;
}

static Tiny_Value Lib_MoveToFirstOccuranceOfString(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 2);

    const char* s = Tiny_ToString(args[0]);

    assert(s);

    bool searchUp = Tiny_ToBool(args[1]);

    return Tiny_NewBool(MoveToFirstOccuranceOfString(ed, s, searchUp));
}

static Tiny_Value Lib_SetTokenColor(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 4);

    int tokenType = (int)Tiny_ToNumber(args[0]);

    assert(tokenType >= 0 && tokenType < NUM_TOKEN_TYPES);

    int r = (int)Tiny_ToNumber(args[1]);
    int g = (int)Tiny_ToNumber(args[2]);
    int b = (int)Tiny_ToNumber(args[3]);

    SetTokenColor(tokenType, r, g, b);

    return Tiny_Null;
}

static Tiny_Value Lib_StoreTempLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Editor* ed = thread->userdata;

    if(ed->numTempLines >= MAX_TEMP_LINES) {
        return Tiny_NewBool(false);
    }

    strcpy(ed->tempLines[ed->numTempLines++], Tiny_ToString(args[0]));
    return Tiny_NewBool(true);
}

static Tiny_Value Lib_ClearTempLines(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    ed->numTempLines = 0;

    return Tiny_Null;
}

static Tiny_Value Lib_TempLineCount(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    return Tiny_NewNumber((int)ed->numTempLines);
}

static Tiny_Value Lib_GetTempLine(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 1);

    int index = (int)Tiny_ToNumber(args[0]);

    assert(index >= 0 && index < ed->numTempLines);

    return Tiny_NewConstString(ed->tempLines[index]);
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

static Tiny_Value Lib_OpenFile(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Editor* ed = thread->userdata;

    if(MyOpenFile(&ed->buf, Tiny_ToString(args[0]))) {
        int fileOpened = Tiny_GetFunctionIndex(ed->state, "file_opened");

        if(fileOpened >= 0) {
            const char* s = Tiny_ToString(args[0]);
            
            const Tiny_Value margs[] = {
                Tiny_NewString(thread, estrdup(s))
            };

            Tiny_CallFunction(thread, fileOpened, margs, 1);
        }

        MoveTo(ed, 0, 0);
        strcpy(ed->filename, Tiny_ToString(args[0]));

        return Tiny_NewBool(true);
    }
    
    return Tiny_NewBool(false);
}

static Tiny_Value Lib_WriteFile(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Editor* ed = thread->userdata;

    if(MyWriteFile(&ed->buf, Tiny_ToString(args[0]))) {
        int fileWritten = Tiny_GetFunctionIndex(ed->state, "file_written");

        if(fileWritten >= 0) {
			const char* s = Tiny_ToString(args[0]);

			const Tiny_Value margs[1] = {
				Tiny_NewString(thread, estrdup(s))
			};

            Tiny_CallFunction(thread, fileWritten, margs, 1);
        }

        return Tiny_NewBool(true);
    } 

    return Tiny_NewBool(false);
}

static Tiny_Value Lib_GetCmd(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    return Tiny_NewConstString(((Editor*)thread->userdata)->cmd);
}

static Tiny_Value Lib_SetStatus(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    StatusTime = 0; 

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

static void SetMode(Editor* ed, Mode mode)
{
    ed->mode = mode;

    if (ed->mode == MODE_VISUAL_LINE) {
        ed->vStart = ed->cur;
    } else if(ed->mode == MODE_COMMAND || ed->mode == MODE_FORWARD_SEARCH) {
        ed->cmd[0] = 0;
    }

    MoveTo(ed, ed->cur.x, ed->cur.y);
}

static Tiny_Value Lib_SetMode(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Editor* ed = thread->userdata;

    SetMode(ed, (int)Tiny_ToNumber(args[0]));

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

    MoveTo(ed, ed->cur.x, ed->cur.y);

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

static Tiny_Value Lib_GetCommand(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
	Editor* ed = thread->userdata;

	return Tiny_NewConstString(ed->cmd);
}

static Tiny_Value Lib_GetFilename(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    return Tiny_NewConstString(ed->filename);
}

static int CountBracesOnLine(Editor* ed, int line, bool* insideComments)
{
    assert(line >= 0 && line < ed->buf.numLines);

    const char* s = ed->buf.lines[line];
   
    bool insideQuotes = false;

    int braces = 0;

    while(*s) {
        if(*s == '"') {
			s += 1;
            insideQuotes = !insideQuotes;
        } else if(*s == '\\') {
            s += 1;
            if(*s == '"') {
                s += 1;
            }
        } else if(*s == '\'') {
            if(s[1] == '\\') {
                // Assumes single character lookahead
                s += 1;
            }

            s += 2;
        } else if(*s == '/' && s[1] == '/') {
            break;
        } else if(*s == '/' && s[1] == '*') {
            *insideComments = true;
			s += 2;
        } else if(*s == '*' && s[1] == '/') {
            *insideComments = false;
			s += 2;
        } else if(!*insideComments && !insideQuotes) {
            if(*s == '{')
				braces += 1;
            else if(*s == '}') braces -= 1;
			s += 1;
		} else {
			s += 1;
		}
    }

    return braces;
}

static Tiny_Value Lib_CountBracesDown(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    Editor* ed = thread->userdata;

    assert(count == 1);

    int line = (int)Tiny_ToNumber(args[0]);

    bool insideComment = false;
    int braces = 0;

    for(int i = 0; i < line; ++i) {
        braces += CountBracesOnLine(ed, i, &insideComment);
    }

    return Tiny_NewNumber((double)braces);
}

static void BindFunctions(Tiny_State* state)
{
    Tiny_BindConstNumber(state, "MODE_INSERT", (double)MODE_INSERT);
    Tiny_BindConstNumber(state, "MODE_NORMAL", (double)MODE_NORMAL);
    Tiny_BindConstNumber(state, "MODE_VISUAL_LINE", (double)MODE_VISUAL_LINE);
    Tiny_BindConstNumber(state, "MODE_COMMAND", (double)MODE_COMMAND);
    Tiny_BindConstNumber(state, "MODE_FORWARD_SEARCH", (double)MODE_FORWARD_SEARCH);

    Tiny_BindConstNumber(state, "TOK_DIRECTIVE", (double)TOK_DIRECTIVE);
    Tiny_BindConstNumber(state, "TOK_IDENT", (double)TOK_IDENT);
    Tiny_BindConstNumber(state, "TOK_KEYWORD", (double)TOK_KEYWORD);
    Tiny_BindConstNumber(state, "TOK_SPACE", (double)TOK_SPACE);
    Tiny_BindConstNumber(state, "TOK_STRING", (double)TOK_STRING);
    Tiny_BindConstNumber(state, "TOK_CHAR", (double)TOK_CHAR);
    Tiny_BindConstNumber(state, "TOK_NUM", (double)TOK_NUM);
    Tiny_BindConstNumber(state, "TOK_DEFINITION", (double)TOK_DEFINITION);
    Tiny_BindConstNumber(state, "TOK_COMMENT", (double)TOK_COMMENT);
    Tiny_BindConstNumber(state, "TOK_SPECIAL_COMMENT", (double)TOK_SPECIAL_COMMENT);

    Tiny_BindStandardArray(state);

    Tiny_BindFunction(state, "exit", Lib_Exit);

    Tiny_BindFunction(state, "strlen", Lib_Strlen);
    Tiny_BindFunction(state, "strspn", Lib_Strspn);
    Tiny_BindFunction(state, "stridx", Lib_Stridx);
    Tiny_BindFunction(state, "substr", Lib_Substr);
    Tiny_BindFunction(state, "strtod", Lib_Strtod);

    Tiny_BindFunction(state, "floor", Lib_Floor);
    Tiny_BindFunction(state, "ceil", Lib_Ceil);

    Tiny_BindFunction(state, "set_token_color", Lib_SetTokenColor);

    Tiny_BindFunction(state, "store_temp_line", Lib_StoreTempLine);
    Tiny_BindFunction(state, "clear_temp_lines", Lib_ClearTempLines);
    Tiny_BindFunction(state, "temp_line_count", Lib_TempLineCount);
    Tiny_BindFunction(state, "get_temp_line", Lib_GetTempLine);

    Tiny_BindFunction(state, "open_file", Lib_OpenFile);
    Tiny_BindFunction(state, "write_file", Lib_WriteFile);

    Tiny_BindFunction(state, "set_status", Lib_SetStatus);
    Tiny_BindFunction(state, "get_command", Lib_GetCommand);

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
    Tiny_BindFunction(state, "move_to_first_occurance_of_string", Lib_MoveToFirstOccuranceOfString);
    Tiny_BindFunction(state, "move", Lib_Move);

    Tiny_BindFunction(state, "read_char", Lib_ReadChar);

    Tiny_BindFunction(state, "get_filename", Lib_GetFilename);
    Tiny_BindFunction(state, "reload_scripts_next_frame", Lib_ReloadScriptsNextFrame);

    Tiny_BindFunction(state, "count_braces_down", Lib_CountBracesDown);
}

static void ReloadScripts(Editor* ed)
{
    if(ed->state) {
        Tiny_DestroyThread(&ed->thread);
        Tiny_DeleteState(ed->state);
    }

    ed->state = Tiny_CreateState();

    BindFunctions(ed->state);

    Tiny_CompileFile(ed->state, "scripts/colors.tiny");
    Tiny_CompileFile(ed->state, "scripts/main.tiny");

    Tiny_InitThread(&ed->thread, ed->state);
    Tiny_StartThread(&ed->thread);

    ed->thread.userdata = ed;

    while(Tiny_ExecuteCycle(&ed->thread));

    ed->updateFunction = Tiny_GetFunctionIndex(ed->state, "update");

    assert(ed->updateFunction >= 0);
}

void InitEditor(Editor* ed)
{
    ed->mode = MODE_NORMAL;

    ed->cur.x = ed->cur.y = 0;

    ed->blinkTime = 0;

    ed->scrollY = 0;

    ed->elapsed = tigrTime();

    InitDefaultBuffer(&ed->buf);

    // Setup script
    ReloadScripts(ed);
}

void FileOpened(Editor* ed, const char* name)
{
    strcpy(ed->filename, name);

    int fileOpened = Tiny_GetFunctionIndex(ed->state, "file_opened");

    MoveTo(ed, 0, 0);

    if(fileOpened >= 0) {
        Tiny_Value args[1] = {
            Tiny_NewString(&ed->thread, estrdup(name))
        };

        Tiny_CallFunction(&ed->thread, fileOpened, args, 1);
    }
}

void UpdateEditor(Editor* ed, Tigr* screen)
{
    if(ed->shouldReloadScripts) {
        ReloadScripts(ed);
        ed->shouldReloadScripts = false;
    }

    ed->screen = screen;

    float diff = tigrTime();

    ed->elapsed += diff;
    ed->blinkTime += diff;

    if(ed->blinkTime > 1.0f) {
        ed->blinkTime = 0;
    }

    if(ed->mode == MODE_COMMAND) {
        ed->blinkTime = 1.0f;
    }

    if(ed->mode == MODE_FORWARD_SEARCH) {
        ed->blinkTime = 0;
    }

    if(ed->mode == MODE_COMMAND || ed->mode == MODE_FORWARD_SEARCH) {
        int ch = tigrReadChar(screen);

        if(ch > 0) {
            if(ch == 10) {
                if(ed->mode == MODE_COMMAND) {
                    // Submit command
                    int handleCommand = Tiny_GetFunctionIndex(ed->state, "handle_command");

                    if(handleCommand >= 0) {
                        const Tiny_Value args[] = {
                            Tiny_NewConstString(ed->cmd)
                        };

                        Tiny_CallFunction(&ed->thread, handleCommand, args, 1);
                    }
                }

                SetMode(ed, MODE_NORMAL);
            } else if (ch == 8) {
                // Backspace
                char* s = ed->cmd;
                while (*s++);

                if (s > ed->cmd) {
                    s -= 2;
                    *s = 0;
                }
            } else if (ch == 27) {
                ed->cmd[0] = 0;
                SetMode(ed, MODE_NORMAL);
            } else {
                char* s = ed->cmd;
                while(*s++);

                if(s >= ed->cmd + MAX_COMMAND_LENGTH) {
                    // It's is too long, just go back to normal mode
                    SetMode(ed, MODE_NORMAL);

                    StatusTime = 0;
                    strcpy(Status, "Text was too long.");
                } else {
                    s -= 1;
                    *s++ = (char)ch;
                    *s = 0;
                }
            }

            if(ed->mode == MODE_FORWARD_SEARCH) {
                int handleSearch = Tiny_GetFunctionIndex(ed->state, "handle_search");

                if(handleSearch >= 0) {
                    Tiny_Value args[1] = {
                        Tiny_NewConstString(ed->cmd)
                    };

                    Tiny_CallFunction(&ed->thread, handleSearch, args, 1);
                }
            }
        }
    } else {
        if(Status[0]) {
            StatusTime += diff;

            if(StatusTime > 2.0f) {
                Status[0] = 0;
                StatusTime = 0;
            }
        }

        int ticks = (int)(ed->elapsed / 0.5f);

        Tiny_CallFunction(&ed->thread, ed->updateFunction, NULL, 0);
    }
}

void DestroyEditor(Editor* ed)
{
    Tiny_DestroyThread(&ed->thread);
    Tiny_DeleteState(ed->state);
}


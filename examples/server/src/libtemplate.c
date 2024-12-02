#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "dict.h"
#include "stretchy_buffer.h"
#include "tiny.h"

#define VAR_SIZE 32

#define ERROR(...)                                                   \
    do {                                                             \
        fprintf(stderr, "Template error %s(%i): ", filename, *line); \
        fprintf(stderr, __VA_ARGS__);                                \
        goto error;                                                  \
    } while (0)

extern Tiny_NativeProp DictProp;
extern Tiny_NativeProp ArrayProp;
extern Tiny_NativeProp BufProp;

static void AppendStr(char** buf, const char* s) {
    size_t len = strlen(s);

    char* start = sb_add(*buf, len);

    for (int i = 0; i < len; ++i) {
        start[i] = s[i];
    }
}

static const Tiny_Value* ReadVar(const char* filename, int* line, FILE* f, Dict* env, Array* arr,
                                 int arrIndex, int c, char* var) {
    const Tiny_Value* val = NULL;

    if (c == '_') {
        if (!arr) {
            ERROR("Attempted to use '$_' outside of array expansion.\n");
        }

        assert(arrIndex >= 0 && arrIndex < ArrayLen(arr));

        var[0] = '_';
        var[1] = 0;

        val = ArrayGet(arr, arrIndex);
    } else {
        int i = 0;

        while (isalpha(c)) {
            if (i >= VAR_SIZE - 1) {
                ERROR("Var name is too long.\n");
            }

            var[i++] = c;
            c = getc(f);
        }
        ungetc(c, f);

        var[i] = 0;

        val = DictGet(env, (Tiny_Value){
                               .type = TINY_VAL_CONST_STRING,
                               .cstr = var,
                           });
    }

    return val;
error:
    return NULL;
}

static bool Expand(const char* filename, int* line, FILE* f, char** pBuf, int end, Dict* env,
                   Array* arr, int arrIndex) {
    char* buf = *pBuf;

    while (true) {
        int c = getc(f);

        if (c == end) {
            break;
        }

        if (c == '\n') ++*line;

        if (c == '[') {
            c = getc(f);

            if (c == '[') {
                sb_push(buf, '[');
                continue;
            }

            char var[VAR_SIZE];

            const Tiny_Value* val = ReadVar(filename, line, f, env, arr, arrIndex, c, var);

            c = getc(f);

            if (c != ']') {
                ERROR("Expected ']' after %s.\n", var);
            }

            c = getc(f);

            if (c != '{') {
                ERROR("Expected '{' after ']'.\n");
            }

            c = getc(f);

            long epos = ftell(f);

            int nest = 1;
            while (c != EOF && nest != 0) {
                if (c == '\n') ++*line;
                if (c == '{') ++nest;
                if (c == '}') --nest;
                c = getc(f);
            }

            if (c == EOF) {
                ERROR("Unexpected EOF.\n");
            }

            long pos = ftell(f);

            if (!val || Tiny_GetProp(*val) != &ArrayProp) {
                ERROR("Attempted to expand '[%s]' but %s is not an array.\n", var, var);
            } else {
                Array* a = Tiny_ToAddr(*val);

                *pBuf = buf;

                for (int i = 0; i < ArrayLen(a); ++i) {
                    fseek(f, epos, SEEK_SET);
                    Expand(filename, line, f, pBuf, '}', env, a, i);
                }

                buf = *pBuf;

                fseek(f, pos, SEEK_SET);
            }
        } else if (c == '?') {
            c = getc(f);

            if (c == '?') {
                sb_push(buf, '?');
                continue;
            }

            char var[VAR_SIZE];

            const Tiny_Value* val = ReadVar(filename, line, f, env, arr, arrIndex, c, var);

            c = getc(f);

            if (c != '{') {
                ERROR("Expected '{' after %s.\n", var);
            }

            c = getc(f);

            long tp = ftell(f);

            int nest = 1;
            while (c != EOF && nest != 0) {
                if (c == '\n') ++*line;
                if (c == '{') ++nest;
                if (c == '}') --nest;
                c = getc(f);
            }

            if (c == EOF) {
                ERROR("Unexpected EOF.\n");
            }

            if (c != '{') {
                ERROR("Expected '{' after '}' in '?%s' clause.\n", var);
            }

            c = getc(f);

            long fp = ftell(f);

            nest = 1;
            while (c != EOF && nest != 0) {
                if (c == '\n') ++*line;
                if (c == '{') ++nest;
                if (c == '}') --nest;
                c = getc(f);
            }

            if (c == EOF) {
                ERROR("Unexpected EOF.\n");
            }

            long pos = ftell(f);

            if (!val || !Tiny_ToBool(*val)) {
                fseek(f, fp, SEEK_SET);
            } else {
                fseek(f, tp, SEEK_SET);
            }

            *pBuf = buf;
            Expand(filename, line, f, pBuf, '}', env, NULL, -1);

            buf = *pBuf;

            fseek(f, pos, SEEK_SET);
        } else if (c == '$') {
            c = getc(f);

            if (c == '$') {
                sb_push(buf, c);
                continue;
            }

            char var[VAR_SIZE];

            const Tiny_Value* val = ReadVar(filename, line, f, env, arr, arrIndex, c, var);

            if (!val) {
                ERROR("Var '%s' doesn't exist in env.\n", var);
            }

            switch (val->type) {
                case TINY_VAL_BOOL: {
                    AppendStr(&buf, val->boolean ? "true" : "false");
                } break;

                case TINY_VAL_INT: {
                    char s[32];
                    sprintf(s, "%i", val->i);
                    AppendStr(&buf, s);
                } break;

                case TINY_VAL_FLOAT: {
                    char s[32];
                    sprintf(s, "%g", val->f);
                    AppendStr(&buf, s);
                } break;

                case TINY_VAL_STRING:
                case TINY_VAL_CONST_STRING: {
                    AppendStr(&buf, Tiny_ToString(*val));
                } break;

                case TINY_VAL_NATIVE: {
                    if (Tiny_GetProp(*val) != &BufProp) {
                        goto defaultCase;
                    }

                    char* b = *(char**)Tiny_ToAddr(*val);

                    for (int i = 0; i < sb_count(b); ++i) {
                        sb_push(buf, b[i]);
                    }
                } break;

                defaultCase:
                default: {
                    ERROR("Attempted to expand unsupported value at '$%s'.\n", var);
                } break;
            }
        } else {
            sb_push(buf, c);
        }
    }

    *pBuf = buf;
    return true;

error:
    *pBuf = buf;
    return false;
}

static TINY_FOREIGN_FUNCTION(RenderTemplate) {
    const char* filename = Tiny_ToString(args[0]);
    Dict* env = Tiny_ToAddr(args[1]);

    FILE* f = fopen(filename, "r");

    if (!f) {
        fprintf(stderr, "Failed to open file '%s' for reading (RenderTemplate).\n", filename);
        return Tiny_Null;
    }

    int line = 1;
    char* buf = NULL;

    if (!Expand(filename, &line, f, &buf, EOF, env, NULL, -1)) {
        fclose(f);
        sb_free(buf);

        return Tiny_Null;
    }

    fclose(f);

    char** bp = malloc(sizeof(char*));
    *bp = buf;

    return Tiny_NewNative(thread, bp, &BufProp);
}

void BindTemplateUtils(Tiny_State* state) {
    Tiny_RegisterType(state, "dict");

    Tiny_BindFunction(state, "render_template(str, dict): buf", RenderTemplate);
}

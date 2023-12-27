#include "lib.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "dict.h"
#include "stretchy_buffer.h"
#include "tiny.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#endif

char* estrdup(const char* s);

static void FinalizeBuf(Tiny_Context* ctx, void* bp) {
    sb_free(*(unsigned char**)bp);
    free(bp);
}

const Tiny_NativeProp BufProp = {
    "buf",

    NULL,
    FinalizeBuf,
};

extern const Tiny_NativeProp DictProp;
extern const Tiny_NativeProp ArrayProp;

static TINY_FOREIGN_FUNCTION(Buf) {
    unsigned char** buf = malloc(sizeof(unsigned char*));
    *buf = NULL;

    if (count == 1) {
        const char* str = Tiny_ToString(args[0]);

        size_t len = strlen(str);

        unsigned char* start = sb_add(*buf, len);

        for (int i = 0; i < len; ++i) {
            start[i] = str[i];
        }
    }

    return Tiny_NewNative(thread, buf, &BufProp);
}

static TINY_FOREIGN_FUNCTION(BufPushByte) {
    unsigned char** buf = Tiny_ToAddr(args[0]);
    unsigned char b = (unsigned char)Tiny_ToInt(args[1]);

    sb_push(*buf, b);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufPushStr) {
    unsigned char** buf = Tiny_ToAddr(args[0]);
    const char* str = Tiny_ToString(args[1]);

    size_t len = strlen(str);

    unsigned char* start = sb_add(*buf, len);

    for (size_t i = 0; i < len; ++i) {
        start[i] = str[i];
    }

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufClear) {
    unsigned char** buf = Tiny_ToAddr(args[0]);

    stb__sbn(*buf) = 0;

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufLen) {
    unsigned char** buf = Tiny_ToAddr(args[0]);
    return Tiny_NewInt(sb_count(*buf));
}

static TINY_FOREIGN_FUNCTION(BufToStr) {
    unsigned char** buf = Tiny_ToAddr(args[0]);

    return Tiny_NewStringCopy(thread, *buf, sb_count(*buf));
}

static TINY_FOREIGN_FUNCTION(GetFileContents) {
    const char* filename = Tiny_ToString(args[0]);

    FILE* file = fopen(filename, "rb");

    if (!file) {
        return Tiny_Null;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    unsigned char** buf = malloc(sizeof(unsigned char*));
    *buf = NULL;

    unsigned char* start = sb_add(*buf, size);

    fread(start, 1, size, file);
    fclose(file);

    return Tiny_NewNative(thread, buf, &BufProp);
}

static TINY_FOREIGN_FUNCTION(FilePutContents) {
    const char* filename = Tiny_ToString(args[0]);
    unsigned char** buf = Tiny_ToAddr(args[1]);

    FILE* file = fopen(filename, "wb");

    if (!file) {
        return Tiny_NewBool(false);
    }

    fwrite(*buf, 1, sb_count(*buf), file);
    fclose(file);

    return Tiny_NewBool(true);
}

static void ConvertPath(char* s) {
#ifdef _WIN32
    while (*s) {
        if (*s == '/') *s = '\\';
        s += 1;
    }
#else
#endif
}

static TINY_FOREIGN_FUNCTION(ListDir) {
    const char* dir = Tiny_ToString(args[0]);

#ifdef _WIN32
    char path[MAX_PATH];

    strcpy(path, dir);
    ConvertPath(path);

    strcat(path, "\\*");

    WIN32_FIND_DATA data;

    HANDLE hFind = FindFirstFile(path, &data);

    if (hFind == INVALID_HANDLE_VALUE) {
        return Tiny_Null;
    }

    Array* a = malloc(sizeof(Array));

    InitArray(a, thread->ctx);

    do {
        if (data.cFileName[0] == '.') continue;

        Tiny_Value val = Tiny_NewStringCopyNullTerminated(thread, data.cFileName);
        ArrayPush(a, val);
    } while (FindNextFile(hFind, &data));

    FindClose(hFind);

    return Tiny_NewNative(thread, a, &ArrayProp);
#else
    return Tiny_Null;
#endif
}

static char* DecodeURL(const char* s) {
    char* buf = NULL;

    while (*s) {
        int c = *s;

        if (c == '%') {
            if (isxdigit(s[1]) && isxdigit(s[2])) {
                sscanf(s + 1, "%2x", &c);
            }

            s += 3;
        } else {
            s += 1;
        }

        sb_push(buf, c);
    }

    sb_push(buf, 0);

    return buf;
}

static TINY_FOREIGN_FUNCTION(Lib_DecodeURL) {
    const char* s = Tiny_ToString(args[0]);
    char* buf = DecodeURL(s);

    return Tiny_NewString(thread, buf, sb_count(buf));
}

static TINY_FOREIGN_FUNCTION(ParseFormValues) {
    char** pBuf = Tiny_ToAddr(args[0]);
    sb_push(*pBuf, 0);

    char* dec = DecodeURL(*pBuf);
    char* startDec = dec;

    char* name = NULL;
    char* value = NULL;

    Dict* d = malloc(sizeof(Dict));

    InitDict(d, thread->ctx);

    while (*dec) {
        while (*dec && *dec != '=') {
            sb_push(name, *dec++);
        }

        sb_push(name, 0);

        if (!*dec) {
            fprintf(stderr, "Expected '=' after value name in form body but it wasn't there.\n");
            sb_free(name);
            break;
        }

        dec += 1;

        while (*dec && *dec != '&') {
            sb_push(value, *dec++);
        }

        if (*dec == '&') ++dec;

        sb_push(value, 0);

        // Handle spaces
        for (int i = 0; i < sb_count(value); ++i) {
            if (value[i] == '+') value[i] = ' ';
        }

        printf("Parsed form value: %s=%s\n", name, value);

        Tiny_Value val = Tiny_NewStringCopyNullTerminated(thread, value);

        DictSet(d, Tiny_NewStringCopyNullTerminated(thread, name), val);

        sb_free(name);
        sb_free(value);

        name = NULL;
        value = NULL;
    }

    sb_free(startDec);

    return Tiny_NewNative(thread, d, &DictProp);
}

void BindBuffer(Tiny_State* state) {
    Tiny_RegisterType(state, "buf");

    Tiny_BindFunction(state, "buf(...): buf", Buf);
    Tiny_BindFunction(state, "buf_push_byte(buf, int): void", BufPushByte);
    Tiny_BindFunction(state, "buf_push_str(buf, str): void", BufPushStr);
    Tiny_BindFunction(state, "buf_len(buf): int", BufLen);
    Tiny_BindFunction(state, "buf_to_str(buf): str", BufToStr);
    Tiny_BindFunction(state, "buf_clear(buf): void", BufClear);
}

void BindIO(Tiny_State* state) {
    Tiny_RegisterType(state, "buf");
    Tiny_RegisterType(state, "array_str");

    Tiny_BindFunction(state, "get_file_contents(str): buf", GetFileContents);
    Tiny_BindFunction(state, "file_put_contents(str, buf): bool", FilePutContents);

    Tiny_BindFunction(state, "list_dir(str): array_str", ListDir);
}

void BindHttpUtils(Tiny_State* state) {
    Tiny_RegisterType(state, "dict");
    Tiny_RegisterType(state, "buf");

    Tiny_BindFunction(state, "decode_url(str): str", Lib_DecodeURL);
    Tiny_BindFunction(state, "parse_form_values(buf): dict", ParseFormValues);
}

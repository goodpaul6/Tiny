#include <string.h>
#include <stdio.h>

#include "stretchy_buffer.h"
#include "tiny.h"
#include "lib.h"

static void FinalizeBuf(void* bp)
{	
	sb_free(*(unsigned char**)bp);
	free(bp);
}

const Tiny_NativeProp BufProp = {
    "buf",

    NULL,
    FinalizeBuf,
};

static TINY_FOREIGN_FUNCTION(Buf)
{
	unsigned char** buf = malloc(sizeof(unsigned char*));
	*buf = NULL;
	
    if(count == 1) {     
        const char* str = Tiny_ToString(args[0]);

        size_t len = strlen(str);

        unsigned char* start = sb_add(*buf, len);
        
        for(int i = 0; i < len; ++i) {
            start[i] = str[i];
        }

        return Tiny_NewNative(thread, buf, &BufProp);
    }

    return Tiny_NewNative(thread, buf, &BufProp);
}

static TINY_FOREIGN_FUNCTION(BufPushByte)
{
    unsigned char** buf = Tiny_ToAddr(args[0]);
    unsigned char b = (unsigned char)Tiny_ToInt(args[1]);

    sb_push(*buf, b);

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufPushStr)
{
    unsigned char** buf = Tiny_ToAddr(args[0]);
    const char* str = Tiny_ToString(args[1]);

    size_t len = strlen(str);

    unsigned char* start = sb_add(*buf, len);

    for(size_t i = 0; i < len; ++i) {
        start[i] = str[i];
    }

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufClear)
{
    unsigned char** buf = Tiny_ToAddr(args[0]);

    stb__sbn(*buf) = 0;

    return Tiny_Null;
}

static TINY_FOREIGN_FUNCTION(BufLen)
{
    unsigned char** buf = Tiny_ToAddr(args[0]);
    return Tiny_NewInt(stb__sbn(*buf));
}

static TINY_FOREIGN_FUNCTION(GetFileContents)
{
    const char* filename = Tiny_ToString(args[0]);

    FILE* file = fopen(filename, "rb");

    if(!file) {
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

void BindBuffer(Tiny_State* state)
{
    Tiny_RegisterType(state, "buf");

    Tiny_BindFunction(state, "buf(...): buf", Buf);
    Tiny_BindFunction(state, "buf_push_byte(buf, int): void", BufPushByte);
    Tiny_BindFunction(state, "buf_push_str(buf, str): void", BufPushStr);
    Tiny_BindFunction(state, "buf_len(buf): int", BufLen);
    Tiny_BindFunction(state, "buf_clear(buf): void", BufClear);
}

void BindIO(Tiny_State* state)
{
    Tiny_RegisterType(state, "buf");

    Tiny_BindFunction(state, "get_file_contents(str): buf", GetFileContents);
}

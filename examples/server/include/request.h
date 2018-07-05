#pragma once

#include <stdbool.h>

#define REQUEST_METHOD_SIZE         64
#define REQUEST_TARGET_SIZE         512
#define REQUEST_VERSION_SIZE        64
#define REQUEST_HEADER_NAME_SIZE    128

typedef struct RequestHeader
{
    struct RequestHeader* next;

    char name[REQUEST_HEADER_NAME_SIZE];
    char* value;
} RequestHeader;

typedef struct
{
    char method[REQUEST_METHOD_SIZE];
    char target[REQUEST_TARGET_SIZE];
    char version[REQUEST_VERSION_SIZE];

    RequestHeader* firstHeader;

    char* body;
} Request;

bool ParseRequest(Request* r, const char* buf);

const char* GetHeaderValue(const Request* r, const char* name);

void DestroyRequest(Request* r);

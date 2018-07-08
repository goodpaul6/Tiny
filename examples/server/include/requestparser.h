#pragma once

#include "request.h"

enum
{
    REQUEST_ERROR = -1
};

typedef enum
{
    REQUEST_STATE_FIRST_LINE,
    REQUEST_STATE_HEADER_NAME,
    REQUEST_STATE_HEADER_VALUE,
    REQUEST_STATE_BODY,
    REQUEST_STATE_DONE
} RequestParserState;

typedef struct
{
    RequestParserState state;

    // If this is -1, then we don't expect a body.
    // Otherwise, we'll be in the body state until 
    // this is done
    int bodyBytesLeft;

	int nameLen;

    RequestHeader curHeader;
} RequestParser;

void InitRequestParser(RequestParser* p);

// Returns the number of bytes read from buf or REQUEST_ERROR
int ParseRequest(RequestParser* p, Request* r, const char* buf, int len);

void DestroyRequestParser(RequestParser* p);

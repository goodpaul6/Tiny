#pragma once

enum
{
    STATUS_OK           = 200,
    STATUS_NOT_FOUND    = 404,
    STATUS_FOUND        = 303
};

char* EncodePlainTextResponse(int status, const char* serverName, const char* content);

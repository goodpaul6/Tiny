#include "response.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAIN_TEXT_BUFLEN 512

static const char* StatusName(int status) {
    switch (status) {
        case STATUS_OK:
            return "OK";
        case STATUS_NOT_FOUND:
            return "Not Found";
        case STATUS_FOUND:
            return "Found";
    }

    assert(0);
    return NULL;
}

char* EncodeResponse(int status, const char* serverName, const char* contentType,
                     const char* content, size_t contentLen) {
    char buf[PLAIN_TEXT_BUFLEN];

    size_t len = sprintf(buf,
                         "HTTP/1.1 %d %s\r\n"
                         "Server: %s\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "Content-Length: %zu\r\n"
                         "Vary: Accept-Encoding\r\n"
                         "Content-Type: %s\r\n\r\n",
                         status, StatusName(status), serverName, contentLen, contentType);

    char* response = malloc(len + contentLen + 3);

    strcpy(response, buf);
    memcpy(response + len, content, contentLen);

    response[len + contentLen] = '\r';
    response[len + contentLen + 1] = '\n';
    response[len + contentLen + 2] = 0;

    return response;
}

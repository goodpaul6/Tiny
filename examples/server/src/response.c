#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "response.h"

#define PLAIN_TEXT_BUFLEN   512

static const char* StatusName(int status)
{
    switch(status) {
        case STATUS_OK:             return "OK";
        case STATUS_NOT_FOUND:      return "Not Found";
        case STATUS_FOUND:          return "Found";
    }

	assert(0);
	return NULL;
}

char* EncodePlainTextResponse(int status, const char* serverName, const char* content)
{
    size_t contentLength = strlen(content);

    char buf[PLAIN_TEXT_BUFLEN];

    size_t len = sprintf(buf, "HTTP/1.1 %d %s\n"
            "Server: %s\n"
            "Accept-Ranges: bytes\n"
            "Content-Length: %zu\n"
            "Vary: Accept-Encoding\n"
            "Content-Type: text/plain\n\n",
            status, StatusName(status),
            serverName,
            contentLength);

    char* response = malloc(len + contentLength + 1);

    strcpy(response, buf);
    strcpy(response + len, content);

    response[len + contentLength] = '\0';
    
    return response;
}

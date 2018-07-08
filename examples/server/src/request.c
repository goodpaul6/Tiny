#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "stretchy_buffer.h"
#include "request.h"

void InitRequest(Request* r)
{
    r->headers = NULL;
    r->body = NULL;
}

const char* GetHeaderValue(const Request* r, const char* name)
{
    for(int i = 0; i < sb_count(r->headers); ++i) {
        if(strcmp(r->headers[i].name, name) == 0) {
            return r->headers[i].value;
        }
    }

    return NULL;
}

void DestroyRequest(Request* r)
{
    for(int i = 0; i < sb_count(r->headers); ++i) {
		sb_free(r->headers[i].value);
    }

    sb_free(r->headers);

    sb_free(r->body);
}

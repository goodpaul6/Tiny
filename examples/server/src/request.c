#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "request.h"

#define S(k) #k
#define X(n) S(n)

bool ParseRequest(Request* r, const char* buf)
{
    r->firstHeader = NULL;
    r->body = NULL;

	int n = sscanf(buf, "%" X(REQUEST_METHOD_SIZE) "s %" X(REQUEST_TARGET_SIZE) "s %" X(REQUEST_VERSION_SIZE) "s", r->method, r->target, r->version);

    if(n != 3) {
        fprintf(stderr, "Failed to parse request start line.\n");
        return false;
    }

	buf = strchr(buf, '\n');

	if (!buf) {
		fprintf(stderr, "Failed to parse request: missing newline after start line.\n");
		return false;
	}

	buf += 1;

    RequestHeader* last = NULL;

    while(true) {
        const char* lineEnd = strchr(buf, '\n');

        if(buf == lineEnd|| (*buf == '\r' && buf + 1 == lineEnd)) {
            // Empty line, end of headers
			buf = lineEnd;
            buf += 1;
            break;
        }

        RequestHeader* header = malloc(sizeof(RequestHeader));

		header->next = NULL;

        if(sscanf(buf, "%" X(REQUEST_HEADER_NAME_SIZE) "s", header->name) != 1) {
            fprintf(stderr, "Failed to parse request header.\n");
            free(header);

            return false;
		}

		buf = strchr(buf, ':');
		
		if (!buf) {
			fprintf(stderr, "Failed to parse request header: missing ':' after name.\n");
			free(header);
			
			RequestHeader* next;
			while (r->firstHeader) {
				free(r->firstHeader->value);

				next = r->firstHeader->next;

				free(r->firstHeader);
				r->firstHeader = next;
			}

			return false;
		}

		// Get rid of the ':'
		header->name[strlen(header->name) - 1] = 0;

		buf += 1;

        // Skip whitespace after name
        while(isspace(*buf)) {
            buf += 1;
        }

        // Read everything until the end of the line
        header->value = malloc(lineEnd - buf + 1);

        int i = 0;
        while(buf < lineEnd) {
			if(*buf == '\r') ++buf;
            else header->value[i++] = *buf++;
        }

        header->value[i] = 0;

        // Skip '\n'
        buf += 1;

        if(!r->firstHeader) {
            r->firstHeader = last = header;
        } else {
            last->next = header;
            last = header;
        }
    }

    size_t len = strlen(buf);

    r->body = malloc(len + 1);
    strcpy(r->body, buf);

    r->body[len] = 0;

    return true;
}

const char* GetHeaderValue(const Request* r, const char* name)
{
    for(const RequestHeader* h = r->firstHeader; h; h = h->next) {
        if(strcmp(h->name, name) == 0) {
            return h->value;
        }
    }

    return NULL;
}

void DestroyRequest(Request* r)
{
    RequestHeader* next;

    for(RequestHeader* h = r->firstHeader; h; h = next) {
        free(h->value);
        next = h->next;
        free(h);
    }

    free(r->body);
}

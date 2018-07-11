#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "request.h"
#include "requestparser.h"
#include "stretchy_buffer.h"

#define S(k) #k
#define X(n) S(n)

void InitRequestParser(RequestParser* p)
{
    p->state = REQUEST_STATE_FIRST_LINE;

    p->bodyBytesLeft = -1;

    p->nameLen = 0;
}

// Returns the number of bytes read from buf or REQUEST_ERROR
int ParseRequest(RequestParser* p, Request* r, const char* buf, int len)
{
    if(p->state == REQUEST_STATE_FIRST_LINE) {
        int n = sscanf(buf, "%" X(REQUEST_METHOD_SIZE) "s %" X(REQUEST_TARGET_SIZE) "s %" X(REQUEST_VERSION_SIZE) "s", r->method, r->target, r->version);

        if(n != 3) {
            fprintf(stderr, "Failed to parse request start line.\n");
            return REQUEST_ERROR;
        }

        const char* lineEnd = strchr(buf, '\n');
        
        if(!lineEnd) {
            fprintf(stderr, "Missing end of line in first line of request.\n");
            return REQUEST_ERROR;
        }

        n = lineEnd - buf + 1;
        buf = lineEnd + 1;

        if(n == len) {
            p->state = REQUEST_STATE_DONE;
        } else {
            p->nameLen = 0;
            p->bodyBytesLeft = 0;
            p->state = REQUEST_STATE_HEADER_NAME;
        }

        return n;
    }

    if(p->state == REQUEST_STATE_HEADER_NAME) {
        int i = 0;

        while(i < len) {  
            if(buf[i] == '\r') {
                if(p->nameLen > 0) {
                    fprintf(stderr, "Invalid request: carriage return in request header name.\n");
                    return REQUEST_ERROR;
                } else {
                    i += 1;
                    continue;
                }
            }
            
            if(buf[i] == '\n') {
                if(p->nameLen > 0) {
                    fprintf(stderr, "Invalid request: newline in request header name.\n");
                    return REQUEST_ERROR;
                } 
                i += 1;

                if(p->bodyBytesLeft > 0) {
                    p->state = REQUEST_STATE_BODY;
				} else {
					p->state = REQUEST_STATE_DONE;
				}

                break;
            }
            
            if(buf[i] == ':') {
                p->state = REQUEST_STATE_HEADER_VALUE;

                p->curHeader.name[p->nameLen] = 0;
                p->curHeader.value = NULL;
                i += 1;

                // We skip the ':' so + 1
                break;
            }

            if(p->nameLen >= REQUEST_HEADER_NAME_SIZE - 1) {
                fprintf(stderr, "Header name in request was too long.\n");
                return REQUEST_ERROR;
            }

            p->curHeader.name[p->nameLen++] = buf[i++];
        }

        return i;
    }

    if(p->state == REQUEST_STATE_HEADER_VALUE) {
        int i = 0;

        while(i < len) {
            if(buf[i] == '\r') {
                i += 1;
                continue;
            }

            if(buf[i] == '\n') {
                if(strcmp(p->curHeader.name, "Content-Length") == 0) {
					sb_push(p->curHeader.value, 0);
                    p->bodyBytesLeft = atoi(p->curHeader.value);
                }

                p->state = REQUEST_STATE_HEADER_NAME;
                p->nameLen = 0;

				sb_push(p->curHeader.value, 0);
                sb_push(r->headers, p->curHeader);
            
                i += 1;
                break;
            }
			
			if (sb_count(p->curHeader.value) == 0 && isspace(buf[i])) {
				i += 1;
				continue;
			}

            sb_push(p->curHeader.value, buf[i]);
            i += 1;
        }

        return i;
    }

    if(p->state == REQUEST_STATE_BODY) {
        int i = 0;

        while(i < len) {
            if(p->bodyBytesLeft == 0) {
                break;
            }

            sb_push(r->body, buf[i]);
            p->bodyBytesLeft -= 1;
			i += 1;
        }

		if(p->bodyBytesLeft == 0) {
			sb_push(r->body, 0);
			p->state = REQUEST_STATE_DONE;
		}
        
        return i;
    }

    assert(0);
    return REQUEST_ERROR;
}

void DestroyRequestParser(RequestParser* p)
{
    if(p->state == REQUEST_STATE_HEADER_VALUE) {
        sb_free(p->curHeader.value);
    }
}

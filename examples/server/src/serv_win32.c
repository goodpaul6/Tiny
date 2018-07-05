#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>

#include "tiny.h"
#include "config.h"
#include "request.h"
#include "response.h"
#include "scriptpool.h"
#include "tinycthread.h"

#define DEFAULT_BUFLEN  4096

typedef struct
{
    Config conf;
    ScriptPool* pool;
} Server;

typedef struct
{
    Server* serv;
    Request request;

	// Socket will be closed when this reaches 0
	int* socketRc;

    SOCKET clientSocket;
} RequestContext;

static volatile bool KeepRunning = true;

static TINY_FOREIGN_FUNCTION(GetRequestMethod)
{
	RequestContext* ctx = thread->userdata;
	return Tiny_NewConstString(ctx->request.method);
}

static TINY_FOREIGN_FUNCTION(GetRequestTarget)
{
	RequestContext* ctx = thread->userdata;
	return Tiny_NewConstString(ctx->request.target);
}

static TINY_FOREIGN_FUNCTION(LibGetHeaderValue)
{
	RequestContext* ctx = thread->userdata;
	const char* value = GetHeaderValue(&ctx->request, Tiny_ToString(args[0]));

	if (!value) {
		return Tiny_Null;
	}

	return Tiny_NewConstString(value);
}

static TINY_FOREIGN_FUNCTION(SendResponse)
{
	RequestContext* ctx = thread->userdata;

	const char* str = Tiny_ToString(args[2]);

	char* s;
	if (count == 3) {
		size_t len = strlen(str);
		s = EncodeResponse(Tiny_ToInt(args[0]), ctx->serv->conf.name, Tiny_ToString(args[1]), Tiny_ToString(args[2]), len);
	} else {
		int len = Tiny_ToInt(args[3]);
		s = EncodeResponse(Tiny_ToInt(args[0]), ctx->serv->conf.name, Tiny_ToString(args[1]), Tiny_ToString(args[2]), len);
	}
	
	int iSendResult = send(ctx->clientSocket, s, strlen(s), 0);
	
	if (iSendResult == SOCKET_ERROR) {
		free(s);
		return Tiny_NewInt(WSAGetLastError());
	}

	free(s);
	return Tiny_Null;
}

static int Listen(void* pServ)
{
    Server* serv = pServ;
    
    // Listen for connections
    WSADATA wsaData;
    SOCKET listenSocket;

    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if(iResult != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", iResult);
        return 1;
    }

    struct addrinfo hints;
    
    ZeroMemory(&hints, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result;

    iResult = getaddrinfo(NULL, serv->conf.port, &hints, &result);
    if(iResult != 0) {
        fprintf(stderr, "getaddrinfo failed: %d\n", iResult);
        WSACleanup();
        return 1;
    }
    
    listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if(listenSocket == INVALID_SOCKET) {
        fprintf(stderr, "Error at socket(): %d\n", WSAGetLastError());
        freeaddrinfo(result); 
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);

    if(iResult == SOCKET_ERROR) {
        fprintf(stderr, "bind failed with error: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if(listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed with error: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
	
	while (KeepRunning) {
		SOCKET clientSocket = accept(listenSocket, NULL, NULL);

		if (clientSocket == INVALID_SOCKET) {
			fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
            continue;
		}

		char recvbuf[DEFAULT_BUFLEN];
		int iSendResult;
		int recvbuflen = DEFAULT_BUFLEN;

		int* rc = malloc(sizeof(int));
		*rc = 1;

		do {
			iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
			InterlockedIncrement(rc);

			if (iResult > 0) {
                // Null terminate buffer
                recvbuf[iResult] = 0; 

                RequestContext* ctx = malloc(sizeof(RequestContext));

                ctx->serv = serv;

				ctx->socketRc = rc;
                ctx->clientSocket = clientSocket;

                if(!ParseRequest(&ctx->request, recvbuf)) {
                    free(ctx);
                    return 1;
                }

                const char* filename = GetFilenameForTarget(&serv->conf, ctx->request.target);

                if(!filename) {
                    free(ctx);

                    // Send 404
					char* resp = EncodeResponse(STATUS_NOT_FOUND, serv->conf.name, "text/plain", "I can't find what you're looking for.",
						strlen("I can't find what you're looking for."));

                    iSendResult = send(clientSocket, resp, iResult, 0);

                    free(resp);

                    if (iSendResult == SOCKET_ERROR) {
                        fprintf(stderr, "send failed: %d\n", WSAGetLastError());
                        closesocket(clientSocket);
                    }
                } else {
                    StartScript(serv->pool, filename, ctx);
                }
			} else if (iResult == 0) {
				printf("Read complete.\n");
			} else {
				fprintf(stderr, "recv failed: %d\n", WSAGetLastError());
			}
		} while (iResult > 0);

		InterlockedDecrement(rc);
		if (*rc == 0) {
			free(rc);
			closesocket(clientSocket);
		}
	}

    WSACleanup();
	return 0;
}

static void InitState(Tiny_State* state)
{
	Tiny_BindConstInt(state, "STATUS_OK", STATUS_OK);
	Tiny_BindConstInt(state, "STATUS_NOT_FOUND", STATUS_NOT_FOUND);
	Tiny_BindConstInt(state, "STATUS_FOUND", STATUS_FOUND);

	Tiny_BindFunction(state, "get_request_method(): str", GetRequestMethod);
	Tiny_BindFunction(state, "get_request_target(): str", GetRequestTarget);
	Tiny_BindFunction(state, "get_request_header_value(str): str", LibGetHeaderValue);

	Tiny_BindFunction(state, "send_response(int, str, str, ...): void", SendResponse);
}

static void FinalizeRequestContext(void* pCtx)
{
    RequestContext* ctx = pCtx;

    int iResult = shutdown(ctx->clientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        fprintf(stderr, "shutdown failed with error: %d\n", WSAGetLastError());
    }

	InterlockedDecrement(ctx->socketRc);

	if (*ctx->socketRc == 0) {
		free(ctx->socketRc);
		closesocket(ctx->clientSocket);
	}

    DestroyRequest(&ctx->request);
}

static void IntHandler(int dummy)
{
    KeepRunning = false;
}

int main(int argc, char** argv) 
{
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <script.tiny>\n", argv[0]);
        return 1;
    }
    
    // TODO(Apaar): Instead of loading a single script, the script
    // supplied will be used to configure the server like so:
    //
    // set_port(8080)
    // 
    // add_route("/static/*", "scripts/static.tiny")
    // add_route("/*", "scripts/wiki.tiny")
    //
    // init_db_connection("wiki.db")
    // if get_argc() == 3 && get_argv(2) == "initdb" {
    //     db_run("drop table users;")
    //     db_run("create table users { ... }")
    // }
    //
    // The first route that matches is executed.
    //
    // How will execution work? I'll store a pool of Tiny_StateThreads
    // and every iteration of the loop below, I'll accept incoming connections
    // and do the routing process, and I'll also run a handful of cycles
    // on all the threads which are alive. Each thread will maintain a context
    // which will store the request and the client socket.
    //
    // Every one of these scripts should do stuff like:
    //
    // if get_request_method() == "GET" {
    //     send_plain_text_response("Hello!")
    // }
    //
    // The database connection will be stored globally (accessible by all threads).
    //

    signal(SIGINT, IntHandler);

    Server serv;

    InitConfig(&serv.conf, argv[1], argc, argv);
    
    if(!serv.conf.name) {
        fprintf(stderr, "Script doesn't specify a server name.\n");
        return 1;
    }

    if(!serv.conf.port) {
        fprintf(stderr, "Script doesn't specify a port.\n");
        return 1;
    }

    serv.pool = CreateScriptPool(serv.conf.numThreads, InitState, FinalizeRequestContext);

    thrd_t listenThread;

    thrd_create(&listenThread, Listen, &serv);

    while(KeepRunning) {
		UpdateScriptPool(serv.pool, 1);
    }

    DeleteScriptPool(serv.pool);

    DestroyConfig(&serv.conf);

    return 0;
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <assert.h>

#include "tiny.h"
#include "request.h"
#include "response.h"

#define DEFAULT_BUFLEN  4096

static Tiny_Value Lib_GetRequestMethod(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Request* r = Tiny_ToAddr(args[0]);
    return Tiny_NewConstString(r->method);
}

static Tiny_Value Lib_GetRequestTarget(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Request* r = Tiny_ToAddr(args[0]);
    return Tiny_NewConstString(r->target);
}

static Tiny_Value Lib_GetHeaderValue(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);

    Request* r = Tiny_ToAddr(args[0]);
    const char* name = Tiny_ToString(args[1]);

    return Tiny_NewConstString(GetHeaderValue(r, name));
}

static Tiny_Value Lib_GetRequestBody(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);

    Request* r = Tiny_ToAddr(args[0]);
    return Tiny_NewConstString(r->body);
}

static Tiny_Value Lib_EncodePlainTextResponse(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 3);

    char* s = EncodePlainTextResponse(Tiny_ToInt(args[0]),
                                      Tiny_ToString(args[1]),
                                      Tiny_ToString(args[2]));

    return Tiny_NewString(thread, s);
}

int main(int argc, char** argv) 
{
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <script.tiny>\n", argv[0]);
        return 1;
    }

	Tiny_State* state = Tiny_CreateState();

    Tiny_RegisterType(state, "Request");

    Tiny_BindFunction(state, "get_request_method(Request): str", Lib_GetRequestMethod);
    Tiny_BindFunction(state, "get_request_target(Request): str", Lib_GetRequestTarget);
    Tiny_BindFunction(state, "get_header_value(Request, str): str", Lib_GetHeaderValue);
    Tiny_BindFunction(state, "get_request_body(Request): str", Lib_GetRequestBody);

    Tiny_BindConstInt(state, "STATUS_OK", STATUS_OK);
    Tiny_BindConstInt(state, "STATUS_NOT_FOUND", STATUS_NOT_FOUND);
    Tiny_BindConstInt(state, "STATUS_FOUND", STATUS_FOUND);

    Tiny_BindFunction(state, "encode_plain_text_response(int, str, str): str", Lib_EncodePlainTextResponse);

	Tiny_CompileFile(state, argv[1]);

    int handleRequest = Tiny_GetFunctionIndex(state, "handle_request");

    if(handleRequest < 0) {
        fprintf(stderr, "Script %s is missing 'handle_request' function.\n", argv[1]);

        Tiny_DeleteState(state);
        return 0;
    }

    Tiny_StateThread thread;
    Tiny_InitThread(&thread, state);

    // Run all the global code in the script
    Tiny_StartThread(&thread);
  
    while (Tiny_ExecuteCycle(&thread));

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

    iResult = getaddrinfo(NULL, "8080", &hints, &result);
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
	
	while (true) {
		SOCKET clientSocket = accept(listenSocket, NULL, NULL);

		if (clientSocket == INVALID_SOCKET) {
			fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
			closesocket(listenSocket);
			WSACleanup();
			return 1;
		}

		char recvbuf[DEFAULT_BUFLEN];
		int iSendResult;
		int recvbuflen = DEFAULT_BUFLEN;

		do {
			iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
			if (iResult > 0) {
                Request r;

                // Null terminate buffer
                recvbuf[iResult] = 0;

                if(!ParseRequest(&r, recvbuf)) {
                    continue;
                }

                Tiny_Value args[1] = {
                    Tiny_NewLightNative(&r)
                };

                Tiny_Value response = Tiny_CallFunction(&thread, handleRequest, args, 1);

                DestroyRequest(&r);

				/*
				const char* response =
					"HTTP/1.1 200 OK\n"
					"Date: Mon, 27 Jul 2009 12:28:53 GMT\n"
					"Server: Apache\n"
					"Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\n"
					"ETag: \"34aa387-d-1568eb00\"\n"
					"Accept-Ranges: bytes\n"
					"Content-Length: 51\n"
					"Vary: Accept-Encoding\n"
					"Content-Type: text/plain\n\n"
					"Hello World! My payload includes a trailing CRLF.\r\n";
				*/

				iSendResult = send(clientSocket, Tiny_ToString(response), iResult, 0);

				if (iSendResult == SOCKET_ERROR) {
					fprintf(stderr, "send failed: %d\n", WSAGetLastError());                    

					closesocket(clientSocket);
					WSACleanup();

					return 1;
				}

				printf("bytes sent: %d\n", iSendResult);
			} else if (iResult == 0) {
				printf("Connection closing...\n");
			} else {
				fprintf(stderr, "recv failed: %d\n", WSAGetLastError());
				closesocket(clientSocket);
				WSACleanup();
				return 1;
			}
		} while (iResult > 0);

		iResult = shutdown(clientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			fprintf(stderr, "shutdown failed with error: %d\n", WSAGetLastError());
			closesocket(clientSocket);
			WSACleanup();
			return 1;
		}

		closesocket(clientSocket);
	}

    WSACleanup();

    Tiny_DestroyThread(&thread);
	Tiny_DeleteState(state);

    return 0;
}

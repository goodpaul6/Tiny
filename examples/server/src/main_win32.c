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

#include "server.h"
#include "sock.h"
#include "stretchy_buffer.h"

volatile bool KeepRunning = true;

int MainLoop(void* pServ);
int ConnLoop(void* pServ);

static void IntHandler(int dummy)
{
    KeepRunning = false;
}

int main(int argc, char** argv)
{
    signal(SIGINT, IntHandler);

    Server serv;

    // Initialize the server
    InitConfig(&serv.conf, argv[1], argc, argv);
    
    if(!serv.conf.name) {
        fprintf(stderr, "Script doesn't specify a server name.\n");
        return 1;
    }

    if(!serv.conf.port) {
        fprintf(stderr, "Script doesn't specify a port.\n");
        return 1;
    }

	printf("Successfully configured server.\n"
		"Name: %s\n"
		"Port: %s\n"
		"Num Threads: %d\n"
		"Cycles Per Loop: %d\n"
		"Num Routes: %d\n", serv.conf.name, serv.conf.port, serv.conf.numThreads, serv.conf.cyclesPerLoop, sb_count(serv.conf.routes));

    InitList(&serv.clientQueue, sizeof(Sock));
    InitList(&serv.requestQueue, sizeof(ClientRequest));

    cnd_init(&serv.updateLoop);

    // The loop thread is responsible for initializing LoopData

    thrd_t loopThread, connThread;

    thrd_create(&loopThread, MainLoop, &serv);
    thrd_create(&connThread, ConnLoop, &serv);

    // Initialize winsock and listen for clients
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

    iResult = getaddrinfo(NULL, serv.conf.port, &hints, &result);
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

		int iMode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &iMode) == SOCKET_ERROR) {
            fprintf(stderr, "ioctlsocket failed: %d\n", WSAGetLastError());
            continue;
        }

        Sock sock;

        InitSock(&sock, (void*)clientSocket);
        
        ListPushBack(&serv.clientQueue, &sock);
	}

    WSACleanup();

    thrd_join(&connThread, NULL);
	thrd_join(&loopThread, NULL);

    cnd_destroy(&serv.updateLoop);

	DestroyList(&serv.requestQueue);
	DestroyList(&serv.clientQueue);

    DestroyConfig(&serv.conf);

	return 0;
}

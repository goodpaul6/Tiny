#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "sock.h"

#include <WinSock2.h>
#include <stdlib.h>

int InitSock(Sock* sock, void* handle) {
#ifdef _WIN32
    sock->rc = malloc(sizeof(int));
    *sock->rc = 1;

    sock->handle = handle;
    return 0;
#else
    return SOCK_ERROR;
#endif
}

int SockRecv(Sock* sock, char* buf, int len) {
#ifdef _WIN32
    int r = recv((SOCKET)sock->handle, buf, len, 0);

    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            return SOCK_WOULD_BLOCK;
        }

        return SOCK_ERROR;
    }

    return r;
#else
    return SOCK_ERROR;
#endif
}

int SockSend(Sock* sock, const char* buf, int len) {
#ifdef _WIN32
    int r = send((SOCKET)sock->handle, buf, len, 0);

    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            return SOCK_WOULD_BLOCK;
        }

        return SOCK_ERROR;
    }

    return r;
#else
    return SOCK_ERROR;
#endif
}

int RetainSock(Sock* sock) {
#ifdef _WIN32
    InterlockedIncrement(sock->rc);
    return 0;
#else
    return SOCK_ERROR;
#endif
}

int ReleaseSock(Sock* sock) {
#ifdef _WIN32
    InterlockedDecrement(sock->rc);
    if (*sock->rc <= 0) {
        free(sock->rc);
        closesocket((SOCKET)sock->handle);
    }

    return 0;
#else
    return SOCK_ERROR;
#endif
}

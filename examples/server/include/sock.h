#pragma once

enum {
    SOCK_ERROR = -1,
    SOCK_WOULD_BLOCK = -2,
};

typedef struct {
    int* rc;
    void* handle;
} Sock;

int InitSock(Sock* sock, void* handle);

int SockRecv(Sock* sock, char* buf, int len);
int SockSend(Sock* sock, const char* buf, int len);

int RetainSock(Sock* sock);
int ReleaseSock(Sock* sock);

#pragma once

enum
{
    SOCK_ERROR = -1
};

typedef struct
{
    int* rc;
    void* handle;
} Sock;

int InitSock(Sock* sock, void* handle);

int SockSend(Sock* sock, const char* buf, int len);

int RetainSock(Sock* sock);
int ReleaseSock(Sock* sock);

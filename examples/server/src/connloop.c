#include <assert.h>
#include <stdlib.h>

#include "server.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#else
#error TODO Other platforms
#endif

static void LoopBody(Server* serv)
{
    Sock client;

    if(ListPopFront(&serv->clientQueue, &client)) {
        for(int i = 0; i < serv->conf.maxConns; ++i) {
            Connection* conn = &serv->conn.conns[i];

            if(conn->active) continue;

            InitRequest(&conn->r);
            InitRequestParser(&conn->p);

            conn->client = client;
            conn->active = true;
			break;
        }
    }

	fd_set fds;

	FD_ZERO(&fds);
	int nfds = 0;

    for(int i = 0; i < serv->conf.maxConns; ++i) {
        Connection* conn = &serv->conn.conns[i];

        if(!conn->active) continue;

		FD_SET((SOCKET)conn->client.handle, &fds);
		++nfds;
    }

    if(nfds == 0) {
        mtx_lock(&serv->clientQueue.mutex);

        while(!serv->clientQueue.head) {
            cnd_wait(&serv->newConn, &serv->clientQueue.mutex);
        }

        mtx_unlock(&serv->clientQueue.mutex);

		return;
    }

	TIMEVAL timeout = { 1, 0 };

    int r = select(nfds, &fds, NULL, NULL, &timeout);

    if (r == SOCKET_ERROR) {
        fprintf(stderr, "select failed: %d\n", WSAGetLastError());
        return;
    }

    if (r != 0) {
        for (int i = 0; i < serv->conf.maxConns; ++i) {
            Connection* conn = &serv->conn.conns[i];

            if (!FD_ISSET((SOCKET)conn->client.handle, &fds)) {
				continue;
            }

            char buf[512];

            int res = SockRecv(&conn->client, buf, 512);

            if (res != SOCK_WOULD_BLOCK) {
                if (res == 0) {
					if (*conn->client.rc == 1) {
						printf("Closing socket.\n");
					}

                    DestroyRequest(&conn->r);
                    DestroyRequestParser(&conn->p);

                    conn->active = false;

                    ReleaseSock(&conn->client);
                    continue;
                }

                int i = 0;
                int len = res;

                while (i < res) {
                    int m = ParseRequest(&conn->p, &conn->r, buf + i, len);
                    i += m;
                    len -= m;

                    if (conn->p.state == REQUEST_STATE_DONE) {
                        // Queue it up yo
                        ClientRequest r;

                        printf("Just got request.\n");

                        r.client = conn->client;

                        RetainSock(&r.client);

                        r.r = conn->r;

                        ListPushBack(&serv->requestQueue, &r);
                        cnd_signal(&serv->updateLoop);

                        InitRequest(&conn->r);
                        InitRequestParser(&conn->p);
                    }
                }
            }
        }
    }
}

extern volatile bool KeepRunning;

int ConnLoop(void* pServ)
{
    Server* serv = pServ;

    serv->conn.conns = malloc(sizeof(Connection) * serv->conf.maxConns);
    
    for(int i = 0; i < serv->conf.maxConns; ++i) {
        serv->conn.conns[i].active = false;
    }

    while(KeepRunning) {
        LoopBody(serv);
    }

    for(int i = 0; i < serv->conf.maxConns; ++i) {
        if(!serv->conn.conns[i].active) continue;

        ReleaseSock(&serv->conn.conns[i].client);
    }

    free(serv->conn.conns);

	return 0;
}

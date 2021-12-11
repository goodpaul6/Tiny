#pragma once

#include "config.h"
#include "list.h"
#include "requestparser.h"
#include "sock.h"
#include "tiny.h"
#include "tinycthread.h"

typedef struct {
    Tiny_State* state;
    char* filename;
    long long writeTime;
} StateFilename;

typedef struct {
    StateFilename* states;  // array

    // length = server.conf.numThreads
    Tiny_StateThread* threads;
} LoopData;

typedef struct {
    Sock client;
    Request r;
} ClientRequest;

typedef struct {
    bool active;
    Sock client;
    Request r;
    RequestParser p;
} Connection;

typedef struct ConnectionData {
    Connection* conns;
} ConnectionData;

typedef struct {
    Config conf;

    // This is filled by the main thread listening
    // for clients. It contains Sock's (client sockets)
    List clientQueue;

    // This is filled by the connection handler thread
    // It contains ClientRequests
    List requestQueue;

    // When a request is parsed or a waiting StateThread finishes its work
    // this is signaled.
    cnd_t updateLoop;

    LoopData loop;

    // Signaled when an accept call succeeds
    cnd_t newConn;

    ConnectionData conn;
} Server;

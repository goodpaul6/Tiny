#pragma once

#include "tiny.h"
#include "list.h"
#include "config.h"
#include "tinycthread.h"
#include "sock.h"

typedef struct
{
    Sock client;

    int len;
    char* buf;
} ReceivedData;

typedef struct
{
    Tiny_State* state;
    char* filename;
    long long writeTime;
} StateFilename;

typedef struct
{
    StateFilename* states;  // array
    
    // length = server.conf.numThreads
    Tiny_StateThread* threads;
} LoopData;

typedef struct
{
    Config conf;
    
    // This is filled by the main thread listening
    // for clients. It contains ReceivedData
    List dataQueue;

    // When some data is received or a waiting StateThread finishes its work
    // this is signaled.
    cnd_t updateLoop;

    LoopData loop;
} Server;

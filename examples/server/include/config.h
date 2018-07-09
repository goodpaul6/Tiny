#pragma once

#include "tiny.h"

typedef struct
{
    char* pattern;
    char* filename;
} Route;

typedef struct
{
    Tiny_ForeignFunction func;
    char* sig;
} ForeignModuleFunction;

typedef struct
{
    void* handle;
    ForeignModuleFunction* funcs;   // array
} ForeignModule;

typedef struct
{
    int argc;
    char** argv;

    char* name;
    char* port;

    int maxConns;

    int numThreads;
    Route* routes;      // array

    int cyclesPerLoop;

    ForeignModule* modules; // array
} Config;

void InitConfig(Config* c, const char* filename, int argc, char** argv);

const char* GetFilenameForTarget(const Config* c, const char* target);

void DestroyConfig(Config* c);

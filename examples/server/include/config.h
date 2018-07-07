#pragma once

typedef struct
{
    char* pattern;
    char* filename;
} Route;

typedef struct
{
    int argc;
    char** argv;

    char* name;
    char* port;

    int numThreads;
    Route* routes;      // array

    int cyclesPerLoop;
} Config;

void InitConfig(Config* c, const char* filename, int argc, char** argv);

const char* GetFilenameForTarget(const Config* c, const char* target);

void DestroyConfig(Config* c);

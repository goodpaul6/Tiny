#include "util.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif

// This is already present in tiny_lib.h
#if 0
char* estrdup(const char* s)
{
    char* ss = malloc(strlen(s) + 1);
    strcpy(ss, s);
    return ss;
}
#endif

bool GetLastWriteTime(const char* filename, long long* time) {
#ifdef _WIN32
    FILETIME c, a, w;
    HANDLE hFile =
        CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!GetFileTime(hFile, &c, &a, &w)) {
        return false;
    }

    *time = ((long long)w.dwHighDateTime << 32 | w.dwLowDateTime);

    CloseHandle(hFile);

    return true;
#else
    return false;
#endif
}

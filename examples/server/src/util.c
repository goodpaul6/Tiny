#include "util.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif

char* estrdup(const char* str) {
    size_t len = strlen(str);

    char* dup = malloc(len + 1);
    memcpy(dup, str, len + 1);

    return dup;
}

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

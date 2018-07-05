#pragma once

#include <stdbool.h>

#include "stretchy_buffer.h"

char* estrdup(const char* s);

bool GetLastWriteTime(const char* filename, long long* time);

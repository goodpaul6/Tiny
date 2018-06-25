#pragma once

#define MAX_STATUS_LENGTH 256
#define LINES_PER_PAGE    40

typedef struct Tigr Tigr;
typedef struct Editor Editor;

extern char Status[MAX_STATUS_LENGTH];

void DrawEditor(Tigr* screen, Editor* editor);

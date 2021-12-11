#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "editor.h"
#include "tigr.h"

static Editor GEditor;
static Tigr* screen;

static bool Done;

void DrawEditor(Tigr* screen, Editor* editor);

int main(int argc, char** argv) {
    screen = tigrWindow(640, 480, "Tiny Notepad", TIGR_FIXED | TIGR_2X);

    tigrSetPostFX(screen, 0, 0, 0.5f, 1.2f);

    InitEditor(&GEditor);

    GEditor.screen = screen;

    if (argc > 1) {
        MyOpenFile(&GEditor.buf, argv[1]);
        FileOpened(&GEditor, argv[1]);
    }

    while (!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));

        UpdateEditor(&GEditor, screen);
        DrawEditor(screen, &GEditor);

        tigrUpdate(screen);
    }

    tigrFree(screen);

    DestroyEditor(&GEditor);

    return 0;
}

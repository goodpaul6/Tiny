#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include "tigr.h"
#include "editor.h"
#include "tinycthread.h"

static Editor GEditor;
static Tigr* screen;

static bool Done;

void DrawEditor(Tigr* screen, Editor* editor);

static int EditorThreadFunc(void* arg)
{
    while(!Done) {
        UpdateEditor(&GEditor, screen);
    }

    return 0;
}

int main(int argc, char** argv)
{
    screen = tigrWindow(640, 480, "Tiny Notepad", TIGR_FIXED | TIGR_2X);
    
    tigrSetPostFX(screen, 0, 0, 0.5f, 1.2f);

    InitEditor(&GEditor);

    GEditor.screen = screen;

    if(argc > 1) {
        MyOpenFile(&GEditor.buf, argv[1]);
        FileOpened(&GEditor, argv[1]);
    }

    thrd_t editorThread;

    thrd_create(&editorThread, EditorThreadFunc, NULL);

    while(!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));
    
        DrawEditor(screen, &GEditor);
        
        tigrUpdate(screen);
    }

    tigrFree(screen);

    Done = true;

    thrd_join(editorThread, NULL);

    DestroyEditor(&GEditor);
    
    return 0;
}

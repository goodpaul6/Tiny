#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#include "tigr.h"
#include "editor.h"
#include "display.h"

int main(int argc, char** argv)
{
    Tigr* screen = tigrWindow(640, 480, "Tiny Notepad", TIGR_FIXED | TIGR_2X);

    tigrSetPostFX(screen, 0, 0, 0.5f, 1.1f);

    static Editor ed;

    InitEditor(&ed);

    if(argc > 1) {
        OpenFile(&ed.buf, argv[1]);
        FileOpened(&ed, argv[1]);
    }

    while(!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(20, 20, 20));
    
        UpdateEditor(&ed, screen);
        DrawEditor(screen, &ed);
        
        tigrUpdate(screen);
    }

    DestroyEditor(&ed);

    tigrFree(screen);

    return 0;
}

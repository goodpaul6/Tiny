#include "tigr.h"
#include "tiny.h"

#define MAX_ENTITIES 100

typedef struct
{
    int hp;
    char glyph;
} Entity;

static Entity Entities[MAX_ENTITIES];

int main(int argc, char** argv)
{
    Tigr* screen = tigrWindow(320, 240, "Hello", 0);

    while (!tigrClosed(screen)) {
        tigrClear(screen, tigrRGB(0x80, 0x90, 0xa0));
        tigrPrint(screen, tfont, 120, 110, tigrRGB(0xff, 0xff, 0xff), "Hello, world.");
        tigrUpdate(screen);
    }

    tigrFree(screen);

    return 0;    return 0;
}

#include <assert.h>

#include "tigr.h"
#include "tiny.h"

#define MAX_ENTITIES 100

typedef enum
{
    ENT_PLAYER,
    NUM_ENTITY_TYPES
} EntityType;

static Tigr* Screen;

static Tiny_State* EntityStates[NUM_ENTITY_TYPES];

typedef struct
{
    int hp;
    double x, y;
    Tiny_StateThread thread;
} Entity;

static Tiny_NativeProp EntityProp = {
    "entity",
    NULL,
    NULL,
    NULL
};

static Tiny_Value IsKeyDown(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
    return Tiny_NewBool(tigrKeyHeld(Screen, (int)Tiny_ToNumber(args[0])) != 0);
}

static Tiny_Value Move(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);

    Entity* e = thread->userdata;

    e->x += Tiny_ToNumber(args[0]);
    e->y += Tiny_ToNumber(args[1]);

    return Tiny_Null;
}

static Tiny_Value GetX(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    if(count == 1) {
        // Another entity's x pos
        assert(Tiny_GetProp(args[0]) == &EntityProp);

        return Tiny_NewNumber(((Entity*)Tiny_ToAddr(args[0]))->x);
    } else {
        // My x
		return Tiny_NewNumber(((Entity*)thread->userdata)->x);
    }
}

static Tiny_Value GetY(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    if(count == 1) {
        // Another entity's y pos
        assert(Tiny_GetProp(args[0]) == &EntityProp);

        return Tiny_NewNumber(((Entity*)Tiny_ToAddr(args[0]))->y);
    } else {
        // My x
		return Tiny_NewNumber(((Entity*)thread->userdata)->y);
    }
}

static Tiny_Value DrawRect(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 7);

    tigrRect(Screen, 
            (int)Tiny_ToNumber(args[0]),
            (int)Tiny_ToNumber(args[1]),
            (int)Tiny_ToNumber(args[2]),
            (int)Tiny_ToNumber(args[3]),
            tigrRGB((int)Tiny_ToNumber(args[4]),
                    (int)Tiny_ToNumber(args[5]),
                    (int)Tiny_ToNumber(args[6])));

    return Tiny_Null;
}

int main(int argc, char** argv)
{
    Screen = tigrWindow(320, 240, "Tiny Game", 0);

    const char* scripts[] = {
        "scripts/player.tiny"
    };

    for(int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        EntityStates[i] = Tiny_CreateState();

        Tiny_BindFunction(EntityStates[i], "is_key_down", IsKeyDown);
        Tiny_BindFunction(EntityStates[i], "move", Move);
        Tiny_BindFunction(EntityStates[i], "get_x", GetX);
        Tiny_BindFunction(EntityStates[i], "get_y", GetY);
        Tiny_BindFunction(EntityStates[i], "draw_rect", DrawRect);

        Tiny_CompileFile(EntityStates[i], scripts[i]);
    }

    Entity player = { 10, 160, 120 };

    Tiny_InitThread(&player.thread, EntityStates[ENT_PLAYER]);
	
	player.thread.userdata = &player;

    int update = Tiny_GetFunctionIndex(EntityStates[ENT_PLAYER], "update");

    while (!tigrClosed(Screen)) {
        tigrClear(Screen, tigrRGB(20, 20, 20));

        Tiny_CallFunction(&player.thread, update, NULL, 0);

        tigrUpdate(Screen);
    }

    tigrFree(Screen);

    return 0;
}

#include <assert.h>
#include <math.h>

#include "tigr.h"
#include "tiny.h"

#define MAX_ENTITIES 100

typedef enum
{
    ENT_PLAYER,
    ENT_CHASER,
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

static Entity Ents[MAX_ENTITIES] = { 0 };

static void AddEnt(int hp, double x, double y, EntityType type)
{
    for(int i = 0; i < MAX_ENTITIES; ++i) {
        if(Ents[i].hp <= 0) {
            Ents[i].hp = hp;
            Ents[i].x = x;
			Ents[i].y = y;
            
            Tiny_InitThread(&Ents[i].thread, EntityStates[type]);
            Ents[i].thread.userdata = &Ents[i];

            break;
        }
    }
}

static Tiny_NativeProp EntityProp = {
    "entity",
    NULL,
    NULL,
    NULL
};

static Tiny_Value GetPlayer(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 0);

    for(int i = 0; i < MAX_ENTITIES; ++i) {
        if(Ents[i].thread.state == EntityStates[ENT_PLAYER]) {
            return Tiny_NewNative(thread, &Ents[i], &EntityProp);
        }
    }

    return Tiny_Null;
}

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
        "scripts/player.tiny",
		"scripts/chaser.tiny"
    };

    for(int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        EntityStates[i] = Tiny_CreateState();

        Tiny_BindFunction(EntityStates[i], "get_player", GetPlayer);
        Tiny_BindFunction(EntityStates[i], "is_key_down", IsKeyDown);
        Tiny_BindFunction(EntityStates[i], "move", Move);
        Tiny_BindFunction(EntityStates[i], "get_x", GetX);
        Tiny_BindFunction(EntityStates[i], "get_y", GetY);
        Tiny_BindFunction(EntityStates[i], "draw_rect", DrawRect);

        Tiny_CompileFile(EntityStates[i], scripts[i]);
    }

    AddEnt(10, 160, 120, ENT_PLAYER);

    for(int i = 0; i < 50; ++i) {
        AddEnt(10, (i % 10) * 10, (i / 10) * 10, ENT_CHASER);
    }
	
    while (!tigrClosed(Screen)) {
        tigrClear(Screen, tigrRGB(20, 20, 20));

        for(int i = 0; i < MAX_ENTITIES; ++i) {
            if(Ents[i].hp <= 0) continue;

            int update = Tiny_GetFunctionIndex(Ents[i].thread.state, "update");
            Tiny_CallFunction(&Ents[i].thread, update, NULL, 0);
        }

        for(int i = 0; i < MAX_ENTITIES; ++i) {
            if(Ents[i].hp <= 0) continue;

            for(int j = i + 1; j < MAX_ENTITIES; ++j) {
                if(Ents[j].hp <= 0) continue;

                float dist2 = (Ents[i].x - Ents[j].x) * (Ents[i].x - Ents[j].x) + 
							  (Ents[i].y - Ents[j].y) * (Ents[i].y - Ents[j].y);

                if(dist2 < 10 * 10) {
                    float angle = atan2(Ents[i].y - Ents[j].y, Ents[i].x - Ents[j].x);
                    float repel = (10 - sqrtf(dist2));

                    float c = cosf(angle);
                    float s = sinf(angle);

                    Ents[i].x += c * repel;
                    Ents[i].y += s * repel;

                    Ents[j].x -= c * repel;
                    Ents[j].y -= s * repel;
                }
            }
        }

        tigrUpdate(Screen);
    }

    tigrFree(Screen);

    return 0;
}

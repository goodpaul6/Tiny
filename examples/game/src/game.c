#include <string.h>
#include <assert.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "tigr.h"
#include "tiny.h"

#define MAX_ENTITIES 200

typedef enum
{
    ENT_PLAYER,
    ENT_CHASER,
    ENT_BULLET,
    ENT_SPAWNER,
    ENT_HUD,
    NUM_ENTITY_TYPES
} EntityType;

static Tigr* Screen;
static int PlayerKills = 0;

static Tiny_State* EntityStates[NUM_ENTITY_TYPES];

typedef struct
{
    int hp;
    double x, y;
    double velX, velY;
    bool playerBullet;
    Tiny_StateThread thread;
} Entity;

static Entity Ents[MAX_ENTITIES] = { 0 };

static Entity* AddEnt(int hp, double x, double y, EntityType type)
{
    for(int i = 0; i < MAX_ENTITIES; ++i) {
        if(Ents[i].hp <= 0) {
            Ents[i].hp = hp;
            Ents[i].x = x;
			Ents[i].y = y;
            
            Ents[i].velX = Ents[i].velY = 0;

            Tiny_InitThread(&Ents[i].thread, EntityStates[type]);
            Ents[i].thread.userdata = &Ents[i];

            // Run it once to initialize globals and such
            Tiny_StartThread(&Ents[i].thread);

            while(Tiny_ExecuteCycle(&Ents[i].thread));

            return &Ents[i];
        }
    }

    return NULL;
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

static Tiny_Value LibAddEnt(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 4);

    AddEnt((int)Tiny_ToNumber(args[0]), 
            Tiny_ToNumber(args[1]), Tiny_ToNumber(args[2]),
            (int)Tiny_ToNumber(args[3]));

	return Tiny_Null;
}

static Tiny_Value AddBullet(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 3);

    double x = Tiny_ToNumber(args[0]);
    double y = Tiny_ToNumber(args[1]);
    double angle = Tiny_ToNumber(args[2]) * (M_PI / 180);

    Entity* b = AddEnt(1, x, y, ENT_BULLET);

    b->playerBullet = thread->state == EntityStates[ENT_PLAYER];
    b->velX = 5 * cos(angle);
    b->velY = 5 * sin(angle);

    return Tiny_Null;
}

static Tiny_Value IsKeyDown(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 1);
    return Tiny_NewBool(tigrKeyHeld(Screen, (int)Tiny_ToNumber(args[0])) != 0);
}

static Tiny_Value Accel(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);

    Entity* e = thread->userdata;

    e->velX += Tiny_ToNumber(args[0]);
    e->velY += Tiny_ToNumber(args[1]);

    return Tiny_Null;
}

static Tiny_Value AccelTowards(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 2);
    assert(Tiny_GetProp(args[0]) == &EntityProp);

    double speed = Tiny_ToNumber(args[1]);

    Entity* e = thread->userdata;
    Entity* o = Tiny_ToAddr(args[0]);

    double angle = atan2(o->y - e->y, o->x - e->x);

    e->velX += cos(angle) * speed;
    e->velY += sin(angle) * speed;

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

static Tiny_Value Kill(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    if(count == 1) {
        assert(Tiny_GetProp(args[0]) == &EntityProp);
        ((Entity*)Tiny_ToAddr(args[0]))->hp = 0;
    } else {
        ((Entity*)thread->userdata)->hp = 0;
    }

    return Tiny_Null;
}

static Tiny_Value LibRand(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 0);

    return Tiny_NewNumber((double)rand());
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

static Tiny_Value DrawText(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count >= 6);

    char buf[128] = { 0 };
    char* s = buf;
    
    const char* fmt = Tiny_ToString(args[5]);

    int arg = 6;

    while(*fmt) {
        if(*fmt == '%') {
            assert(arg < count);

            ++fmt;

            switch(*fmt) {
                case 'g': {
                    s += sprintf(s, "%g", Tiny_ToNumber(args[arg++]));
                } break;
            }
			
			++fmt;
		} else {
			*s++ = *fmt++;
		}
    }

    int w = tigrTextWidth(tfont, buf);
    int h = tigrTextHeight(tfont, buf);

    tigrPrint(Screen, tfont, (int)Tiny_ToNumber(args[0]) - w / 2, (int)Tiny_ToNumber(args[1]) - h / 2,
            tigrRGB((int)Tiny_ToNumber(args[2]),
                    (int)Tiny_ToNumber(args[3]),
                    (int)Tiny_ToNumber(args[4])),
            buf);

    return Tiny_Null;
}

static Tiny_Value GetPlayerKills(Tiny_StateThread* thread, const Tiny_Value* args, int count)
{
    assert(count == 0);

    return Tiny_NewNumber((double)PlayerKills);
}

int main(int argc, char** argv)
{
    srand(time(NULL));
    
    Screen = tigrWindow(320, 240, "Tiny Game", 0);

    const char* scripts[] = {
        "scripts/player.tiny",
		"scripts/chaser.tiny",
        "scripts/bullet.tiny",
        "scripts/spawner.tiny",
        "scripts/hud.tiny"
    };

    for(int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        EntityStates[i] = Tiny_CreateState();

        Tiny_BindConstNumber(EntityStates[i], "ENT_PLAYER", ENT_PLAYER);
        Tiny_BindConstNumber(EntityStates[i], "ENT_CHASER", ENT_CHASER);
        Tiny_BindConstNumber(EntityStates[i], "ENT_BULLET", ENT_BULLET);
        Tiny_BindConstNumber(EntityStates[i], "ENT_SPAWNER", ENT_BULLET);

        Tiny_BindConstNumber(EntityStates[i], "KEY_LEFT", TK_LEFT);
        Tiny_BindConstNumber(EntityStates[i], "KEY_RIGHT", TK_RIGHT);
        Tiny_BindConstNumber(EntityStates[i], "KEY_UP", TK_UP);
        Tiny_BindConstNumber(EntityStates[i], "KEY_DOWN", TK_DOWN);

        Tiny_BindFunction(EntityStates[i], "get_player_kills", GetPlayerKills);

        Tiny_BindFunction(EntityStates[i], "get_player", GetPlayer);
        Tiny_BindFunction(EntityStates[i], "add_ent", LibAddEnt);
        Tiny_BindFunction(EntityStates[i], "add_bullet", AddBullet);
        Tiny_BindFunction(EntityStates[i], "is_key_down", IsKeyDown);
        Tiny_BindFunction(EntityStates[i], "accel", Accel);
        Tiny_BindFunction(EntityStates[i], "accel_towards", AccelTowards);
        Tiny_BindFunction(EntityStates[i], "get_x", GetX);
        Tiny_BindFunction(EntityStates[i], "get_y", GetY);
        Tiny_BindFunction(EntityStates[i], "kill", Kill);
        Tiny_BindFunction(EntityStates[i], "draw_rect", DrawRect);
        Tiny_BindFunction(EntityStates[i], "draw_text", DrawText);
        Tiny_BindFunction(EntityStates[i], "rand", LibRand);

        Tiny_CompileFile(EntityStates[i], scripts[i]);
    }

    AddEnt(10, -100, -100, ENT_HUD);

    AddEnt(10, 160, 120, ENT_PLAYER);

    AddEnt(1, 20, 20, ENT_SPAWNER);
    AddEnt(1, 300, 20, ENT_SPAWNER);
    AddEnt(1, 300, 220, ENT_SPAWNER);
    AddEnt(1, 20, 220, ENT_SPAWNER);

    float lastTime = tigrTime();
    float acc = 0;

    const float timePerFrame = 1.0f / 60.0f;
	
    tigrSetPostFX(Screen, 2, 2, 1.0f, 1);
    
    while (!tigrClosed(Screen)) {
        tigrClear(Screen, tigrRGB(20, 20, 20));

        acc += tigrTime() - lastTime;

        while(acc >= timePerFrame) {
            acc -= timePerFrame;
            
            for(int i = 0; i < MAX_ENTITIES; ++i) {
                if(Ents[i].hp <= 0) continue;

                int update = Tiny_GetFunctionIndex(Ents[i].thread.state, "update");
                Tiny_CallFunction(&Ents[i].thread, update, NULL, 0);

                Ents[i].x += Ents[i].velX;
                Ents[i].y += Ents[i].velY;

                if(Ents[i].thread.state == EntityStates[ENT_BULLET]) continue;

                Ents[i].velX *= 0.5f;
                Ents[i].velY *= 0.5f;
            }

            for(int i = 0; i < MAX_ENTITIES; ++i) {
                if(Ents[i].hp <= 0) continue;

                for(int j = i + 1; j < MAX_ENTITIES; ++j) {
                    if(Ents[j].hp <= 0) continue;

                    if(Ents[i].thread.state == EntityStates[ENT_SPAWNER] ||
                       Ents[j].thread.state == EntityStates[ENT_SPAWNER]) {
                        continue;
                    }

                    float dist2 = (Ents[i].x - Ents[j].x) * (Ents[i].x - Ents[j].x) + 
                        (Ents[i].y - Ents[j].y) * (Ents[i].y - Ents[j].y);

                    if(dist2 < 10 * 10) {
                        if(Ents[i].thread.state == EntityStates[ENT_BULLET] ||
                            Ents[j].thread.state == EntityStates[ENT_BULLET]) {
                            
                            if(Ents[i].thread.state == Ents[j].thread.state) continue;
                            
                            if(Ents[i].thread.state == EntityStates[ENT_BULLET] &&
                               Ents[i].playerBullet &&
                               Ents[j].thread.state == EntityStates[ENT_PLAYER]) {
                                continue;
                            }

                            if(Ents[j].thread.state == EntityStates[ENT_BULLET] &&
                               Ents[j].playerBullet &&
                               Ents[i].thread.state == EntityStates[ENT_PLAYER]) {
                                continue;
                            }

                            Ents[i].hp -= 1;
                            Ents[j].hp -= 1;

                            if(Ents[i].thread.state == EntityStates[ENT_BULLET] &&
                               Ents[i].playerBullet &&
                               Ents[j].hp <= 0) {
                                PlayerKills += 1;
                            }

                            if(Ents[j].thread.state == EntityStates[ENT_BULLET] &&
                               Ents[j].playerBullet &&
                               Ents[i].hp <= 0) {
                                PlayerKills += 1;
                            }
                        } else {
                            float angle = atan2(Ents[i].y - Ents[j].y, Ents[i].x - Ents[j].x);
                            float repel = (10 - sqrtf(dist2));

                            float c = cosf(angle);
                            float s = sinf(angle);

                            Ents[i].velX += c * repel;
                            Ents[i].velY += s * repel;

                            Ents[j].velX -= c * repel;
                            Ents[j].velY -= s * repel;
                        }
                    }
                }
            }
        }

        tigrUpdate(Screen);
    }

    for(int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        Tiny_DeleteState(EntityStates[i]);
    }

    tigrFree(Screen);

    return 0;
}

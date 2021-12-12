#include <assert.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "tigr.h"
#include "tiny.h"

#define MAX_ENTITIES 200

typedef enum { POWER_MULTI_SHOT, NUM_POWERS } PowerupType;

typedef enum {
    ENT_PLAYER,
    ENT_CHASER,
    ENT_BULLET,
    ENT_SPAWNER,
    ENT_HUD,
    ENT_POWERUP,
    NUM_ENTITY_TYPES
} EntityType;

static Tigr* Screen;
static int PlayerKills = 0;

static Tiny_State* EntityStates[NUM_ENTITY_TYPES];

typedef struct {
    int hp;
    float x, y;
    float velX, velY;
    bool playerBullet;
    Tiny_StateThread thread;
} Entity;

static Entity Ents[MAX_ENTITIES] = {0};

static Entity* AddEnt(int hp, float x, float y, EntityType type) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (Ents[i].hp <= 0) {
            if (Ents[i].hp == -1) Tiny_DestroyThread(&Ents[i].thread);

            Ents[i].hp = hp;
            Ents[i].x = x;
            Ents[i].y = y;

            Ents[i].velX = Ents[i].velY = 0;

            Tiny_InitThread(&Ents[i].thread, EntityStates[type]);
            Ents[i].thread.userdata = &Ents[i];

            // Run it once to initialize globals and such
            Tiny_StartThread(&Ents[i].thread);

            while (Tiny_ExecuteCycle(&Ents[i].thread))
                ;

            return &Ents[i];
        }
    }

    return NULL;
}

static Tiny_Value GetPlayer(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 0);

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (Ents[i].thread.state == EntityStates[ENT_PLAYER]) {
            return Tiny_NewLightNative(&Ents[i]);
        }
    }

    return Tiny_Null;
}

static Tiny_Value LibAddEnt(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 4);

    AddEnt((int)Tiny_ToNumber(args[0]), Tiny_ToNumber(args[1]), Tiny_ToNumber(args[2]),
           (int)Tiny_ToNumber(args[3]));

    return Tiny_Null;
}

static Tiny_Value AddBullet(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 3);

    float x = Tiny_ToNumber(args[0]);
    float y = Tiny_ToNumber(args[1]);
    float angle = Tiny_ToNumber(args[2]) * (float)(M_PI / 180);

    Entity* b = AddEnt(1, x, y, ENT_BULLET);

    if (b) {
        b->playerBullet = thread->state == EntityStates[ENT_PLAYER];
        b->velX = 5 * cosf(angle);
        b->velY = 5 * sinf(angle);
    }

    return Tiny_Null;
}

static Tiny_Value IsKeyDown(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 1);
    return Tiny_NewBool(tigrKeyHeld(Screen, (int)Tiny_ToNumber(args[0])) != 0);
}

static Tiny_Value AccelAngle(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 2);

    float angle = Tiny_ToNumber(args[0]) * (float)(M_PI / 180);
    float speed = Tiny_ToNumber(args[1]);

    Entity* e = thread->userdata;

    e->velX += cosf(angle) * speed;
    e->velY += sinf(angle) * speed;

    return Tiny_Null;
}

static Tiny_Value Accel(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 2);

    Entity* e = thread->userdata;

    e->velX += Tiny_ToNumber(args[0]);
    e->velY += Tiny_ToNumber(args[1]);

    return Tiny_Null;
}

static Tiny_Value AccelTowards(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 2);

    float speed = Tiny_ToNumber(args[1]);

    Entity* e = thread->userdata;
    Entity* o = Tiny_ToAddr(args[0]);

    float angle = atan2f(o->y - e->y, o->x - e->x);

    e->velX += cosf(angle) * speed;
    e->velY += sinf(angle) * speed;

    return Tiny_Null;
}

static Tiny_Value GetX(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    if (count == 1) {
        // Another entity's x pos
        return Tiny_NewFloat(((Entity*)Tiny_ToAddr(args[0]))->x);
    } else {
        // My x
        return Tiny_NewFloat(((Entity*)thread->userdata)->x);
    }
}

static Tiny_Value GetY(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    if (count == 1) {
        // Another entity's y pos
        return Tiny_NewFloat(((Entity*)Tiny_ToAddr(args[0]))->y);
    } else {
        // My x
        return Tiny_NewFloat(((Entity*)thread->userdata)->y);
    }
}

static Tiny_Value Kill(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    Entity* e = NULL;

    if (count == 1) {
        e = Tiny_ToAddr(args[0]);
    } else {
        e = thread->userdata;
    }

    // This means its marked for destruction
    e->hp = -1;

    return Tiny_Null;
}

static Tiny_Value LibRand(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 0);

    return Tiny_NewInt(rand());
}

static Tiny_Value DrawRect(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 7);

    tigrRect(Screen, (int)Tiny_ToNumber(args[0]), (int)Tiny_ToNumber(args[1]),
             (int)Tiny_ToNumber(args[2]), (int)Tiny_ToNumber(args[3]),
             tigrRGB(Tiny_ToInt(args[4]), Tiny_ToInt(args[5]), Tiny_ToInt(args[6])));

    return Tiny_Null;
}

static Tiny_Value DrawText(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count >= 6);

    char buf[128] = {0};
    char* s = buf;

    const char* fmt = Tiny_ToString(args[5]);

    int arg = 6;

    while (*fmt) {
        if (*fmt == '%') {
            assert(arg < count);

            ++fmt;

            switch (*fmt) {
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

    tigrPrint(Screen, tfont, (int)Tiny_ToNumber(args[0]) - w / 2,
              (int)Tiny_ToNumber(args[1]) - h / 2,
              tigrRGB((int)Tiny_ToNumber(args[2]), (int)Tiny_ToNumber(args[3]),
                      (int)Tiny_ToNumber(args[4])),
              buf);

    return Tiny_Null;
}

static Tiny_Value GetPlayerKills(Tiny_StateThread* thread, const Tiny_Value* args, int count) {
    assert(count == 0);

    return Tiny_NewInt(PlayerKills);
}

int main(int argc, char** argv) {
    srand(time(NULL));

    Screen = tigrWindow(320, 240, "Tiny Game", 0);

    const char* scripts[] = {"scripts/player.tiny",  "scripts/chaser.tiny", "scripts/bullet.tiny",
                             "scripts/spawner.tiny", "scripts/hud.tiny",    "scripts/powerup.tiny"};

    for (int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        EntityStates[i] = Tiny_CreateState();

        Tiny_BindConstInt(EntityStates[i], "ENT_PLAYER", ENT_PLAYER);
        Tiny_BindConstInt(EntityStates[i], "ENT_CHASER", ENT_CHASER);
        Tiny_BindConstInt(EntityStates[i], "ENT_BULLET", ENT_BULLET);
        Tiny_BindConstInt(EntityStates[i], "ENT_SPAWNER", ENT_BULLET);

        Tiny_BindConstInt(EntityStates[i], "POWER_MULTI_SHOT", POWER_MULTI_SHOT);

        Tiny_BindConstInt(EntityStates[i], "KEY_LEFT", TK_LEFT);
        Tiny_BindConstInt(EntityStates[i], "KEY_RIGHT", TK_RIGHT);
        Tiny_BindConstInt(EntityStates[i], "KEY_UP", TK_UP);
        Tiny_BindConstInt(EntityStates[i], "KEY_DOWN", TK_DOWN);

        Tiny_RegisterType(EntityStates[i], "ent");

        Tiny_BindFunction(EntityStates[i], "get_player_kills(): int", GetPlayerKills);

        Tiny_BindFunction(EntityStates[i], "get_player(): ent", GetPlayer);
        Tiny_BindFunction(EntityStates[i], "add_ent", LibAddEnt);
        Tiny_BindFunction(EntityStates[i], "add_bullet", AddBullet);
        Tiny_BindFunction(EntityStates[i], "is_key_down(int): bool", IsKeyDown);
        Tiny_BindFunction(EntityStates[i], "accel(float, float): void", Accel);
        Tiny_BindFunction(EntityStates[i], "accel_angle(float, float): void", AccelAngle);
        Tiny_BindFunction(EntityStates[i], "accel_towards", AccelTowards);
        Tiny_BindFunction(EntityStates[i], "get_x(): float", GetX);
        Tiny_BindFunction(EntityStates[i], "get_y(): float", GetY);
        Tiny_BindFunction(EntityStates[i], "kill", Kill);
        Tiny_BindFunction(EntityStates[i], "draw_rect", DrawRect);
        Tiny_BindFunction(EntityStates[i], "draw_text", DrawText);
        Tiny_BindFunction(EntityStates[i], "rand(): int", LibRand);

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

    tigrSetPostFX(Screen, 2, 2, 0.5f, 1);

    while (!tigrClosed(Screen)) {
        tigrClear(Screen, tigrRGB(20, 20, 20));

        acc += tigrTime() - lastTime;

        while (acc >= timePerFrame) {
            acc -= timePerFrame;

            for (int i = 0; i < MAX_ENTITIES; ++i) {
                if (Ents[i].hp <= 0) continue;

                int update = Tiny_GetFunctionIndex(Ents[i].thread.state, "update");
                Tiny_CallFunction(&Ents[i].thread, update, NULL, 0);

                Ents[i].x += Ents[i].velX;
                Ents[i].y += Ents[i].velY;

                if (Ents[i].thread.state == EntityStates[ENT_BULLET]) continue;

                Ents[i].velX *= 0.5f;
                Ents[i].velY *= 0.5f;
            }

            for (int i = 0; i < MAX_ENTITIES; ++i) {
                if (Ents[i].hp <= 0) continue;

                for (int j = i + 1; j < MAX_ENTITIES; ++j) {
                    if (Ents[i].hp <= 0) break;
                    if (Ents[j].hp <= 0) continue;

                    if (Ents[i].thread.state == EntityStates[ENT_SPAWNER] ||
                        Ents[j].thread.state == EntityStates[ENT_SPAWNER]) {
                        continue;
                    }

                    float dist2 = (Ents[i].x - Ents[j].x) * (Ents[i].x - Ents[j].x) +
                                  (Ents[i].y - Ents[j].y) * (Ents[i].y - Ents[j].y);

                    if (dist2 < 10 * 10) {
                        if (Ents[i].thread.state == EntityStates[ENT_BULLET] ||
                            Ents[j].thread.state == EntityStates[ENT_BULLET]) {
                            if (Ents[i].thread.state == Ents[j].thread.state) continue;

                            if (Ents[i].thread.state == EntityStates[ENT_BULLET] &&
                                Ents[i].playerBullet &&
                                Ents[j].thread.state == EntityStates[ENT_PLAYER]) {
                                continue;
                            }

                            if (Ents[j].thread.state == EntityStates[ENT_BULLET] &&
                                Ents[j].playerBullet &&
                                Ents[i].thread.state == EntityStates[ENT_PLAYER]) {
                                continue;
                            }

                            Ents[i].hp -= 1;
                            Ents[j].hp -= 1;

                            if (Ents[i].hp <= 0) Tiny_DestroyThread(&Ents[i].thread);
                            if (Ents[j].hp <= 0) Tiny_DestroyThread(&Ents[j].thread);

                            if (Ents[i].thread.state == EntityStates[ENT_BULLET] &&
                                Ents[i].playerBullet && Ents[j].hp <= 0) {
                                PlayerKills += 1;
                            }

                            if (Ents[j].thread.state == EntityStates[ENT_BULLET] &&
                                Ents[j].playerBullet && Ents[i].hp <= 0) {
                                PlayerKills += 1;
                            }
                        } else {
                            float angle = atan2f(Ents[i].y - Ents[j].y, Ents[i].x - Ents[j].x);
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

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (Ents[i].hp > 0 || Ents[i].hp == -1) {
            Tiny_DestroyThread(&Ents[i].thread);
        }
    }

    for (int i = 0; i < NUM_ENTITY_TYPES; ++i) {
        Tiny_DeleteState(EntityStates[i]);
    }

    tigrFree(Screen);

    return 0;
}

#include "common.h"
#include "vm.h"

typedef struct Tiny_Object
{
    struct Tiny_Object* next;
    char data[];
} Tiny_Object;

inline static void PushNull(Tiny_StateThread* thread)
{
    thread->sp = ALIGN_UP_PTR(thread->sp, alignof(void*));

    *(void**)(thread->sp) = NULL;
    thread->sp += sizeof(void*);
}

inline static void PushBool(Tiny_StateThread* thread, bool b)
{
    *thread->sp++ = (char)b;
}

inline static void PushChar(Tiny_StateThread* thread, char c)
{
    *thread->sp++ = (char)c;
}

inline static void PushInt(Tiny_StateThread* thread, int i)
{
    thread->sp = ALIGN_UP_PTR(thread->sp, alignof(int));

    *(int*)thread->sp = i;
}

inline static void PushFloat(Tiny_StateThread* thread, float f)
{
    thread->sp = ALIGN_UP_PTR(thread->sp, alignof(float));

    *(float*)thread->sp = f;
}

inline static void PushString(Tiny_StateThread* thread, const char* str, size_t len)
{
    thread->sp = ALIGN_UP_PTR(thread->sp, alignof(const char*));

    Tiny_StringPoolInsertLen(&thread->stringPool, str, len);
}

static void Run(Tiny_StateThread* thread)
{
    switch(*thread->pc++) {
        case TINY_OP_PUSH_NULL: {
            PushNull(thread);
        } break;

        case TINY_OP_SP_ADD: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            thread->sp += *(int*)thread->pc;
        } break;

        case TINY_OP_PUSH_TRUE: {
            PushBool(true);
        } break;

        case TINY_OP_PUSH_FALSE: {
            PushBool(false);
        } break;
        
        case TINY_OP_PUSH_INT: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
        } break;
    }
}

void Tiny_Run(Tiny_StateThread* thread)
{
}












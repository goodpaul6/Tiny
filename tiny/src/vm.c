#include "state.h"
#include "vm.h"

static void PushValue(Tiny_StateThread* thread, Tiny_Value value)
{
    *(Tiny_Value*)thread->sp = value;
    thread->sp += sizeof(Tiny_Value);
}

static Tiny_Value PopValue(Tiny_StateThread* thread) 
{
    thread->sp -= sizeof(Tiny_Value);
    return *(Tiny_Value*)thread->sp;
}

static void PushNull(Tiny_StateThread* thread)
{
    Tiny_Value v = { .p = NULL };
    PushValue(thread, v);
}

static void PushBool(Tiny_StateThread* thread, bool b)
{
    Tiny_Value v = { .b = b };
    PushValue(thread, v);
}

static void PushChar(Tiny_StateThread* thread, char c)
{
    Tiny_Value v = { .c = c };
    PushValue(thread, v);
}

static void PushInt(Tiny_StateThread* thread, int i)
{
    Tiny_Value v = { .i = i };
    PushValue(thread, v);
}

static void PushFloat(Tiny_StateThread* thread, float f)
{
    Tiny_Value v = { .f = f };
    PushValue(thread, v);
}

static void PushString(Tiny_StateThread* thread, const char* str, size_t len)
{
    const char* s = Tiny_StringPoolInsertLen(thread->stringPool, str, len);

    Tiny_Value v = { .s = s };
    PushValue(thread, v);
}

static void PopFrame(Tiny_StateThread* thread)
{
    assert(thread->fc > 0);

    thread->fc -= 1;
    thread->pc = thread->frames[thread->fc].pc;
    thread->sp = thread->fp;
    thread->fp = thread->frames[thread->fc].fp;

    thread->sp -= thread->frames[thread->fc] * sizeof(Tiny_Value);
}

static void ExecuteCycle(Tiny_StateThread* thread)
{
    switch(*thread->pc++) {
        case TINY_OP_ADD_SP: {
            thread->sp += sizeof(Tiny_Value) * (*thread->pc++);
        } break;

        case TINY_OP_PUSH_NULL: {
            PushNull(thread);
        } break;

        case TINY_OP_PUSH_TRUE: {
            PushBool(thread, true);
        } break;

        case TINY_OP_PUSH_FALSE: {
            PushBool(thread, false);
        } break;
        
        case TINY_OP_PUSH_C: {
            PushChar(thread, *thread->pc++);
        } break;

        case TINY_OP_PUSH_I: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            PushInt(thread, *(int*)thread->pc);
            thread->pc += sizeof(int);
        } break;

        case TINY_OP_PUSH_I_0: {
            PushInt(thread, 0);
        } break;

        case TINY_OP_PUSH_F: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            PushFloat(thread, thread->state->numbers[*(int*)thread->pc]);
            thread->pc += sizeof(int);
        } break;

        case TINY_OP_PUSH_F_0: {
            PushFloat(thread, 0);
        } break;

        case TINY_OP_PUSH_S: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(uintptr_t));
            uintptr_t ps = *(uintptr_t*)thread->pc;
            thread->pc += sizeof(uintptr_t);

            const Tiny_String* s = Tiny_GetString((const char*)ps);
            PushString(thread, s->str, s->len);
        } break;

#define ARITH_OP(type, name, operator, push) \
        case name: { \
            Tiny_Value b = PopValue(thread); \
            Tiny_Value a = PopValue(thread); \
            push(thread, a.type operator b.type); \
        } break;

#define INT_OP(name, operator) \
        ARITH_OP(i, name, operator, PushInt)

#define INT_CMP_OP(name, operator) \
        ARITH_OP(i, name, operator, PushBool)

#define FLOAT_OP(name, operator) \
        ARITH_OP(f, name, operator, PushFloat)

#define FLOAT_CMP_OP(name, operator) \
        ARITH_OP(f, name, operator, PushBool)

#define BOOL_OP(name, operator) \
        ARITH_OP(b, name, operator, PushBool)

#define BOOL_CMP_OP(name, operator) \
        ARITH_OP(b, name, operator, PushBool)

#define CHAR_CMP_OP(name, operator) \
        ARITH_OP(c, name, operator, PushChar)

        INT_OP(TINY_OP_ADD_I, +)
        INT_OP(TINY_OP_SUB_I, -)
        INT_OP(TINY_OP_MUL_I, *)
        INT_OP(TINY_OP_DIV_I, /)
        INT_OP(TINY_OP_MOD_I, %)
        INT_OP(TINY_OP_OR_I, |)
        INT_OP(TINY_OP_AND_I, &)

        case TINY_OP_ADD1_I: {
            *(int*)(thread->sp - sizeof(int)) += 1;
        } break;

        case TINY_OP_SUB1_I: {
            *(int*)(thread->sp - sizeof(int)) -= 1;
        } break;

        INT_CMP_OP(TINY_OP_LT_I, <)
        INT_CMP_OP(TINY_OP_LTE_I, <=)
        INT_CMP_OP(TINY_OP_GT_I, >)
        INT_CMP_OP(TINY_OP_GTE_I, >=)

        FLOAT_OP(TINY_OP_ADD_F, +)
        FLOAT_OP(TINY_OP_SUB_F, -)
        FLOAT_OP(TINY_OP_MUL_F, *)
        FLOAT_OP(TINY_OP_DIV_F, /)

        FLOAT_CMP_OP(TINY_OP_LT_F, <)
        FLOAT_CMP_OP(TINY_OP_LTE_F, <=)
        FLOAT_CMP_OP(TINY_OP_GT_F, >)
        FLOAT_CMP_OP(TINY_OP_GTE_F, >=)

        BOOL_CMP_OP(TINY_OP_EQU_B, ==)
        CHAR_CMP_OP(TINY_OP_EQU_C, ==)
        INT_CMP_OP(TINY_OP_EQU_I, ==)
        FLOAT_CMP_OP(TINY_OP_EQU_F, ==)

        BOOL_OP(TINY_OP_LOG_AND, &&)
        BOOL_OP(TINY_OP_LOG_OR, ||)

#undef BOOL_OP
#undef BOOL_CMP_OP
#undef CHAR_CMP_OP
#undef FLOAT_CMP_OP
#undef FLOAT_OP
#undef INT_CMP_OP
#undef INT_OP
#undef ARITH_OP

        case TINY_OP_LOG_NOT: {
            bool b = PopValue(thread).b;
            PushBool(thread, !b);
        } break;

        case TINY_OP_GOTO: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            int dest = *(int*)thread->pc;
            thread->pc = thread->state->code + dest;
        } break;

        case TINY_OP_GOTO_FALSE: {
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            int dest = *(int*)thread->pc;
            thread->pc += sizeof(int);

            bool b = PopValue(thread).b;
            if(!b) {
                thread->pc = thread->state->code + dest;
            }
        } break;

        case TINY_OP_CALL: {
            uint8_t nargs = *thread->pc++;
            thread->pc = ALIGN_UP_PTR(thread->pc, alignof(int));
            int dest = *(int*)thread->pc;
            thread->pc += sizeof(int);

            assert(thread->fc < TINY_THREAD_MAX_CALL_DEPTH);

            thread->frames[thread->fc++] = (Tiny_Frame){ thread->pc, thread->fp, nargs };
            thread->fp = thread->sp;

            thread->pc = thread->state->code + dest;
        } break;

        case TINY_OP_RET: {
            PopFrame(thread);
        } break;

        case TINY_OP_RETVAL: {
            thread->retval = PopValue(thread);
            PopFrame(thread);
        } break;

        case TINY_OP_GET_RETVAL: {
            PushValue(thread, thread->retval);
        } break;

        case TINY_OP_HALT: {
            thread->pc = NULL;
        } break;

        case TINY_OP_FILE: {
        } break;

        case TINY_OP_LINE: {
        } break;

        case TINY_OP_MISALIGNED_INSTRUCTION: {
            assert(false);
        } break;

        default: {
            assert(false);
        } break;
    }
}

void Tiny_Run(Tiny_StateThread* thread)
{
    thread->pc = thread->state->code;
    while(thread->pc) {
        ExecuteCycle(thread);
    }
}

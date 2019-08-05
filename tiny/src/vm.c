#include "state.h"
#include "opcodes.h"
#include "vm.h"

static void PushValue(Tiny_VM* vm, Tiny_Value value)
{
    *(Tiny_Value*)vm->sp = value;
    vm->sp += sizeof(Tiny_Value);
}

static Tiny_Value PopValue(Tiny_VM* vm) 
{
    vm->sp -= sizeof(Tiny_Value);
    return *(Tiny_Value*)vm->sp;
}

static void PushNull(Tiny_VM* vm)
{
    Tiny_Value v = { .p = NULL };
    PushValue(vm, v);
}

static void PushBool(Tiny_VM* vm, bool b)
{
    Tiny_Value v = { .b = b };
    PushValue(vm, v);
}

static void PushChar(Tiny_VM* vm, char c)
{
    Tiny_Value v = { .c = c };
    PushValue(vm, v);
}

static void PushInt(Tiny_VM* vm, int i)
{
    Tiny_Value v = { .i = i };
    PushValue(vm, v);
}

static void PushFloat(Tiny_VM* vm, float f)
{
    Tiny_Value v = { .f = f };
    PushValue(vm, v);
}

static void PushString(Tiny_VM* vm, const char* str, size_t len)
{
    const char* s = Tiny_StringPoolInsertLen(vm->stringPool, str, len);

    Tiny_Value v = { .s = s };
    PushValue(vm, v);
}

static void PopFrame(Tiny_VM* vm)
{
    assert(vm->fc > 0);

    vm->fc -= 1;
    vm->pc = vm->frames[vm->fc].pc;
    vm->sp = vm->fp;
    vm->fp = vm->frames[vm->fc].fp;

    vm->sp -= vm->frames[vm->fc] * sizeof(Tiny_Value);
}

static void ExecuteCycle(Tiny_VM* vm)
{
    switch(*vm->pc++) {
        case TINY_OP_ADD_SP: {
            vm->sp += sizeof(Tiny_Value) * (*vm->pc++);
        } break;

        case TINY_OP_PUSH_NULL: {
            PushNull(vm);
        } break;

        case TINY_OP_PUSH_TRUE: {
            PushBool(vm, true);
        } break;

        case TINY_OP_PUSH_FALSE: {
            PushBool(vm, false);
        } break;
        
        case TINY_OP_PUSH_C: {
            PushChar(vm, *vm->pc++);
        } break;

        case TINY_OP_PUSH_I: {
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(int));
            PushInt(vm, *(int*)vm->pc);
            vm->pc += sizeof(int);
        } break;

        case TINY_OP_PUSH_I_0: {
            PushInt(vm, 0);
        } break;

        case TINY_OP_PUSH_F: {
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(int));
            PushFloat(vm, vm->state->numbers[*(int*)vm->pc]);
            vm->pc += sizeof(int);
        } break;

        case TINY_OP_PUSH_F_0: {
            PushFloat(vm, 0);
        } break;

        case TINY_OP_PUSH_S: {
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(uintptr_t));
            uintptr_t ps = *(uintptr_t*)vm->pc;
            vm->pc += sizeof(uintptr_t);

            const Tiny_String* s = Tiny_GetString((const char*)ps);
            PushString(vm, s->str, s->len);
        } break;

#define ARITH_OP(type, name, operator, push) \
        case name: { \
            Tiny_Value b = PopValue(vm); \
            Tiny_Value a = PopValue(vm); \
            push(vm, a.type operator b.type); \
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
            *(int*)(vm->sp - sizeof(int)) += 1;
        } break;

        case TINY_OP_SUB1_I: {
            *(int*)(vm->sp - sizeof(int)) -= 1;
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
            bool b = PopValue(vm).b;
            PushBool(vm, !b);
        } break;

        case TINY_OP_GOTO: {
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(int));
            int dest = *(int*)vm->pc;
            vm->pc = vm->state->code + dest;
        } break;

        case TINY_OP_GOTO_FALSE: {
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(int));
            int dest = *(int*)vm->pc;
            vm->pc += sizeof(int);

            bool b = PopValue(vm).b;
            if(!b) {
                vm->pc = vm->state->code + dest;
            }
        } break;

        case TINY_OP_CALL: {
            uint8_t nargs = *vm->pc++;
            vm->pc = ALIGN_UP_PTR(vm->pc, alignof(int));
            int dest = *(int*)vm->pc;
            vm->pc += sizeof(int);

            assert(vm->fc < TINY_THREAD_MAX_CALL_DEPTH);

            vm->frames[vm->fc++] = (Tiny_Frame){ vm->pc, vm->fp, nargs };
            vm->fp = vm->sp;

            vm->pc = vm->state->code + dest;
        } break;

        case TINY_OP_RET: {
            PopFrame(vm);
        } break;

        case TINY_OP_RETVAL: {
            vm->retval = PopValue(vm);
            PopFrame(vm);
        } break;

        case TINY_OP_GET_RETVAL: {
            PushValue(vm, vm->retval);
        } break;

        case TINY_OP_HALT: {
            vm->pc = NULL;
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

void Tiny_Run(Tiny_VM* vm)
{
    vm->pc = vm->state->code;
    while(vm->pc) {
        ExecuteCycle(vm);
    }
}

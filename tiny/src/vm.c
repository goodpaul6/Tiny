#include "state.h"
#include "opcodes.h"
#include "vm.h"

#define INIT_GC_THRESH  8

typedef struct Tiny_Object {
    struct 
    {
        uint8_t marked : 1;
        uint8_t type : 7;
    };

    struct Tiny_Object* next;

    union
    {
        // Only boxed when "any" is used
        bool b;
        uint32_t c;
        int i;
        float f;

        // Always boxed
        const char* s;

        // TODO(Apaar): Arrays, like structs, will be in place, we just reallocate
        // the entire object struct.
        //
        // Maps too. Tiny_Value's should be able to fit in the void* values.
        //
        // "Structs" just in-place arrays of Tiny_Value
    };
} Tiny_Object;

static void Mark(Tiny_VM* vm, Tiny_Object* obj)
{
    // Handle null or already marked values
    if(!obj || obj->marked) {
        return;
    }
    
    obj->marked = 1;

    switch(obj->type) {
        case TINY_VAL_STRING: Tiny_StringPoolRetain(vm->stringPool, obj->s); break;
        default: break;
    }
}

static void MarkFrame(Tiny_VM* vm, Tiny_Value* fp, LocalRoots roots)
{
    // TODO(Apaar): Get rid of this check once we have the compiler _always_
    // generating roots.
    if(!roots.indices) {
        return;
    }

    for(int i = 0; i < BUF_LEN(roots.indices); ++i) {
		Mark(vm, fp[roots.indices[i]].o);
    }
}

static void MarkAll(Tiny_VM* vm)
{
    if(vm->fp) {
        MarkFrame(vm, vm->fp, vm->roots);

        // Note that there could be double marking because a caller might store a reference
        // in a local variable and also pass it as an argument to a function. Both are
        // roots. Arguments need to be roots too because a temporary might be passed in.
        for(int i = 0; i < vm->fc; ++i) {
            Tiny_Frame* frame = &vm->frames[i];
            MarkFrame(vm, frame->fp, frame->roots);
        }
    }

    for(int i = 0; i < BUF_LEN(vm->state->globalRoots.indices); ++i) {
        Mark(vm, vm->globals[vm->state->globalRoots.indices[i]].o);
    }
}

static void DeleteObject(Tiny_VM* vm, Tiny_Object* obj)
{
    if(!obj) {
        return;
    }

    switch(obj->type) {
        case TINY_VAL_STRING: Tiny_StringPoolRelease(vm->stringPool, obj->s); break;
        default: break;
    }
}

static void Sweep(Tiny_VM* vm)
{
    Tiny_Object** node = &vm->gcHead;

    while(*node) {
        if(!(*node)->marked) {
            Tiny_Object* unreachable = *node;
            *node = unreachable->next;

            DeleteObject(vm, unreachable);
        } else {
            (*node)->marked = 0;
            node = &(*node)->next;
        }
    }
}

static void CollectGarbage(Tiny_VM* vm)
{
    MarkAll(vm);
    Sweep(vm);

    vm->maxNumObjects = (vm->numObjects > 0) ? (vm->numObjects * 2) : INIT_GC_THRESH;
}

static Tiny_Object* AllocObject(Tiny_VM* vm, Tiny_ValueType type)
{
    if(vm->numObjects >= vm->maxNumObjects) {
        CollectGarbage(vm);
    }

    Tiny_Object* obj = TMalloc(vm->ctx, sizeof(Tiny_Object));

    obj->marked = 0;
    obj->type = type;

    obj->next = vm->gcHead;
    vm->gcHead = obj;

	vm->numObjects += 1;

    return obj;
}

static void PushValue(Tiny_VM* vm, Tiny_Value value)
{
    *vm->sp++ = value;
}

static Tiny_Value PopValue(Tiny_VM* vm) 
{
    return *(--vm->sp);
}

static void PushNull(Tiny_VM* vm)
{
    Tiny_Value v = { .o = NULL };
    PushValue(vm, v);
}

static void PushBool(Tiny_VM* vm, bool b)
{
    Tiny_Value v = { .b = b };
    PushValue(vm, v);
}

static void PushChar(Tiny_VM* vm, uint32_t c)
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

static void PushString(Tiny_VM* vm, uint64_t key, const char* str, size_t len)
{
    Tiny_Object* o = AllocObject(vm, TINY_VAL_STRING);
    o->s = Tiny_StringPoolInsertKeyLen(vm->stringPool, key, str, len);

    Tiny_Value v = { .o = o };
    PushValue(vm, v);
}

static void PopFrame(Tiny_VM* vm)
{
    assert(vm->fc > 0);

    vm->fc -= 1;
    vm->pc = vm->frames[vm->fc].pc;
    vm->sp = vm->fp;
    vm->fp = vm->frames[vm->fc].fp;

    vm->sp -= vm->frames[vm->fc].nargs;
}

static void ExecuteCycle(Tiny_VM* vm)
{
#define DECODE_VALUE(type, name) \
    type name; \
    do { \
        vm->pc = ALIGN_UP_PTR(vm->pc, alignof(type)); \
        name = *(type*)vm->pc; \
    } while(0)

#define DECODE_VALUE_MOVE_PC(type, name) \
    DECODE_VALUE(type, name); \
    do { \
        vm->pc += sizeof(type); \
    } while(0)

    switch(*vm->pc++) {
        case OP_PUSH_EMPTY_N: {
            uint8_t n = *vm->pc++;
            memset(vm->sp, 0, n * sizeof(*vm->sp));
            vm->sp += n;
        } break;

        case OP_PUSH_NULL: {
            PushNull(vm);
        } break;

        case OP_PUSH_TRUE: {
            PushBool(vm, true);
        } break;

        case OP_PUSH_FALSE: {
            PushBool(vm, false);
        } break;
        
        case OP_PUSH_C: {
            DECODE_VALUE_MOVE_PC(uint32_t, c);
            PushChar(vm, c);
        } break;

        case OP_PUSH_I: {
            DECODE_VALUE_MOVE_PC(int, i);
            PushInt(vm, i);
        } break;

        case OP_PUSH_I_0: {
            PushInt(vm, 0);
        } break;

        case OP_PUSH_F: {
            DECODE_VALUE_MOVE_PC(float, f);
            PushFloat(vm, f);
        } break;

        case OP_PUSH_F_0: {
            PushFloat(vm, 0);
        } break;

        case OP_PUSH_S: {
            DECODE_VALUE_MOVE_PC(uintptr_t, p);

            const Tiny_String* s = Tiny_GetString((const char*)p);
            PushString(vm, s->key, s->str, s->len);
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

        INT_OP(OP_ADD_I, +)
        INT_OP(OP_SUB_I, -)
        INT_OP(OP_MUL_I, *)
        INT_OP(OP_DIV_I, /)
        INT_OP(OP_MOD_I, %)
        INT_OP(OP_OR_I, |)
        INT_OP(OP_AND_I, &)

        case OP_ADD1_I: {
            (vm->sp - 1)->i += 1;
        } break;

        case OP_SUB1_I: {
            (vm->sp - 1)->i -= 1;
        } break;

        INT_CMP_OP(OP_LT_I, <)
        INT_CMP_OP(OP_LTE_I, <=)
        INT_CMP_OP(OP_GT_I, >)
        INT_CMP_OP(OP_GTE_I, >=)

        FLOAT_OP(OP_ADD_F, +)
        FLOAT_OP(OP_SUB_F, -)
        FLOAT_OP(OP_MUL_F, *)
        FLOAT_OP(OP_DIV_F, /)

        FLOAT_CMP_OP(OP_LT_F, <)
        FLOAT_CMP_OP(OP_LTE_F, <=)
        FLOAT_CMP_OP(OP_GT_F, >)
        FLOAT_CMP_OP(OP_GTE_F, >=)

        BOOL_CMP_OP(OP_EQU_B, ==)
        CHAR_CMP_OP(OP_EQU_C, ==)
        INT_CMP_OP(OP_EQU_I, ==)

        case OP_EQU_I_0: {
            bool b = (--vm->sp)->i == 0;
            PushBool(vm, b);
        } break;

        FLOAT_CMP_OP(OP_EQU_F, ==)

        BOOL_OP(OP_LOG_AND, &&)
        BOOL_OP(OP_LOG_OR, ||)

#undef BOOL_OP
#undef BOOL_CMP_OP
#undef CHAR_CMP_OP
#undef FLOAT_CMP_OP
#undef FLOAT_OP
#undef INT_CMP_OP
#undef INT_OP
#undef ARITH_OP

        case OP_LOG_NOT: {
            bool b = PopValue(vm).b;
            PushBool(vm, !b);
        } break;

        case OP_GOTO: {
            DECODE_VALUE(uint32_t, dest);

            vm->pc = vm->state->code + dest;
        } break;

        case OP_GOTO_FALSE: {
            DECODE_VALUE_MOVE_PC(uint32_t, dest);

            bool b = PopValue(vm).b;
            if(!b) {
                vm->pc = vm->state->code + dest;
            }
        } break;

        case OP_CALL: {
            uint8_t nargs = *vm->pc++;
            DECODE_VALUE_MOVE_PC(uint32_t, idx);

            assert(idx < BUF_LEN(vm->state->functionPcs));
            assert(vm->fc < TINY_THREAD_MAX_CALL_DEPTH);

            vm->frames[vm->fc++] = (Tiny_Frame){ vm->pc, vm->fp, nargs, vm->roots };
            vm->fp = vm->sp;
            vm->roots = vm->state->localRoots[idx];

            vm->pc = vm->state->code + vm->state->functionPcs[idx];
        } break;

        case OP_GET: {
            DECODE_VALUE_MOVE_PC(uint32_t, idx);

            assert(idx < BUF_LEN(vm->state->parser.sym.globals));
            PushValue(vm, vm->globals[idx]);
        } break;

        case OP_SET: {
            DECODE_VALUE_MOVE_PC(uint32_t, idx);

            vm->globals[idx] = PopValue(vm);
        } break;

        case OP_GET_LOCAL: {
            int8_t off;
			memcpy(&off, vm->pc++, sizeof(int8_t));

            PushValue(vm, vm->fp[off]);
        } break;

        case OP_SET_LOCAL: {
            int8_t off;
            memcpy(&off, vm->pc++, sizeof(int8_t));

            vm->fp[off] = PopValue(vm);
        } break;

        case OP_RET: {
            PopFrame(vm);
        } break;

        case OP_RETVAL: {
            // TODO(Apaar): Determine if the returned value needs to be marked.
            // Generally, if the retval is to be used, there should be no collection
            // occurring between this instruction and OP_GET_RETVAL, so for now
            // I won't bother marking it.
            vm->retVal = PopValue(vm);
            PopFrame(vm);
        } break;

        case OP_GET_RETVAL: {
            PushValue(vm, vm->retVal);
        } break;

        case OP_HALT: {
            vm->pc = NULL;
        } break;

        case OP_FILE: {
        } break;

        case OP_LINE: {
        } break;

        case OP_MISALIGNED_INSTRUCTION: {
            assert(false);
        } break;

        default: {
            assert(false);
        } break;
    }

#undef DECODE_VALUE_MOVE_PC
#undef DECODE_VALUE
}

void Tiny_InitVM(Tiny_VM* vm, Tiny_Context* ctx, const Tiny_State* state, Tiny_StringPool* sp)
{
    vm->ctx = ctx;
    vm->state = state;

    vm->stringPool = sp;

    vm->gcHead = NULL;
    vm->numObjects = 0;
    vm->maxNumObjects = 8;

    // TODO(Apaar): Memset globals to 0?
    vm->globals = TMalloc(ctx, sizeof(Tiny_Value) * BUF_LEN(state->parser.sym.globals));

    vm->pc = NULL;
    vm->sp = vm->stack;
    vm->fp = NULL;
	
	vm->roots.indices = NULL;

    vm->fc = 0;

    vm->fileName = NULL;
	vm->lineNumber = 0;

    vm->userdata = NULL;
}

void Tiny_Run(Tiny_VM* vm)
{
    vm->pc = vm->state->code;
    while(vm->pc) {
        ExecuteCycle(vm);
    }
}

void Tiny_DestroyVM(Tiny_VM* vm)
{
    // Sweep twice without marking, everything should get collected
    for(int i = 0; i < 2; ++i) {
        Sweep(vm);
    }

    TFree(vm->ctx, vm->globals);
}

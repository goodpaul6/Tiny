#include "vm.hpp"

#include <cstring>

#include "opcode.hpp"

#define TINY_FORWARD(v)  \
    do {                 \
        auto res = (v);  \
        if (res) {       \
            return *res; \
        }                \
    } while (0)

namespace tiny {

VM::VM(CodeView code, StringsView strings)
    : m_code{std::move(code)}, m_strings{std::move(strings)} {}

VM::ExecutionResult VM::execute_cycle() {
    auto& frame = top_frame();

    auto op = static_cast<Opcode>(m_code[frame.pc++]);

    switch (op) {
        case Opcode::OP_PUSH_NULL: {
            auto r = push(std::monostate{});
            if (r) {
                return *r;
            }
        } break;

        case Opcode::OP_PUSH_TRUE: {
            auto r = push(true);
            if (r) {
                return *r;
            }
        } break;

        case Opcode::OP_PUSH_FALSE: {
            auto r = push(false);
            if (r) {
                return *r;
            }
        } break;

        case Opcode::OP_PUSH_CHAR: {
            char32_t c;
            std::memcpy(&c, &m_code[frame.pc], sizeof(c));

            auto r = push(c);
            if (r) {
                return *r;
            }

            frame.pc += sizeof(c);
        } break;

        case Opcode::OP_PUSH_INT: {
            std::int64_t i = 0;
            std::memcpy(&i, &m_code[frame.pc], sizeof(i));

            auto r = push(i);
            if (r) {
                return *r;
            }

            frame.pc += sizeof(i);
        } break;

        case Opcode::OP_PUSH_FLOAT: {
            double f = 0;
            std::memcpy(&f, &m_code[frame.pc], sizeof(f));

            auto r = push(f);
            if (r) {
                return *r;
            }

            frame.pc += sizeof(f);
        } break;

        case Opcode::OP_PUSH_STRING: {
            std::uint32_t i = 0;
            std::memcpy(&i, &m_code[frame.pc], sizeof(i));

            // TODO Create error to be returned when the string index is out of bounds

            auto r = push(m_strings[i]);
            if (r) {
                return *r;
            }

            frame.pc += sizeof(i);
        } break;

        case Opcode::OP_ADD_I: {
            Value a, b;

        } break;
    }
}

VM::Frame& VM::top_frame() { return m_frames[m_frame_count - 1]; }

std::optional<VM::StackOverflowError> VM::push(Value value) {
    if (top_frame().sp + 1 >= STACK_SIZE) {
        return VM::StackOverflowError{};
    }

    m_stack[top_frame().sp++] = std::move(value);

    return std::nullopt;
}

std::optional<VM::StackUnderflowError> VM::pop(Value& value) {
    if (top_frame().sp == 0) {
        return VM::StackUnderflowError{};
    }

    value = m_stack[--top_frame().sp];

    return std::nullopt;
}

}  // namespace tiny

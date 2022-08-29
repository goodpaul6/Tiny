#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "span.hpp"

namespace tiny {

struct VM {
    static constexpr std::size_t MAX_FRAMES = 64;
    static constexpr std::size_t STACK_SIZE = 128;

    using Value = std::variant<std::monostate, bool, char32_t, std::int64_t, double, std::string>;

    struct StackOverflowError {};
    struct StackUnderflowError {};
    struct Done {};

    using ExecutionResult =
        std::variant<std::monostate, StackOverflowError, StackUnderflowError, Done>;

    using CodeView = Span<const std::uint8_t>;
    using StringsView = Span<const std::string>;

    VM(CodeView code, StringsView strings);

    ExecutionResult execute_cycle();

private:
    struct Frame {
        std::int32_t pc = 0;
        std::uint32_t sp = 0;
    };

    CodeView m_code;
    StringsView m_strings;

    std::array<Frame, MAX_FRAMES> m_frames;
    std::uint32_t m_frame_count = 1;

    std::array<Value, STACK_SIZE> m_stack;

    Frame& top_frame();

    std::optional<StackOverflowError> push(Value value);
    std::optional<StackUnderflowError> pop(Value& value);
};

}  // namespace tiny

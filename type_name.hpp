#pragma once

#include <variant>
#include <vector>

#include "primitive_type.hpp"

namespace tiny {

// This is used to represent types in the AST and not the
// fully realized types used for semantic checking.
struct TypeName {
    friend struct TypeNamePool;

    struct Array {
        const TypeName* element = nullptr;
    };

    struct Map {
        const TypeName* key = nullptr;
        const TypeName* value = nullptr;
    };

    struct Function {
        const TypeName* return_value = nullptr;
        std::vector<const TypeName*> args;
    };

    template <typename T>
    TypeName(T&& value) : m_value{std::forward<T>(value)} {}

private:
    using Value = std::variant<PrimitiveType, Array, Map, Function>;

    Value m_value;
};

}  // namespace tiny

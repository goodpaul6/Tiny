#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace tiny {

struct AST;

struct ASTInterpreter {
    using Value = std::variant<std::monostate, bool, char, std::int64_t, float, std::string>;
    using Env = std::unordered_map<std::string, Value>;

    Env env;

    Value eval(AST& ast);
};

}  // namespace tiny

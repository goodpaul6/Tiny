#pragma once

#include <string>

namespace tiny {

struct TypeName;

struct VarDecl {
    std::string name;
    const TypeName* type = nullptr;
};

}  // namespace tiny

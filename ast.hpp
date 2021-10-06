#pragma once

#include <memory>
#include <string>
#include <variant>

#include "pos.hpp"
#include "token_type.hpp"

namespace tiny {

struct ASTVisitor;

struct AST {
    Pos pos;

    virtual ~AST() = default;

    virtual void visit(ASTVisitor&) = 0;
};

struct LiteralAST final : AST {
    std::variant<std::monostate, TokenType, bool, char, std::int64_t, float, std::string> value;

    void visit(ASTVisitor& v) override;
};

struct IdAST final : AST {
    std::string name;

    void visit(ASTVisitor& v) override;
};

struct BinAST final : AST {
    TokenType op;

    std::unique_ptr<AST> lhs;
    std::unique_ptr<AST> rhs;

    void visit(ASTVisitor& v) override;
};

}  // namespace tiny

#pragma once

#include <memory>
#include <string>
#include <variant>

#include "pos.hpp"
#include "token_kind.hpp"
#include "type_name.hpp"
#include "var_decl.hpp"

namespace tiny {

struct ASTVisitor;

struct AST {
    Pos pos;

    virtual ~AST() = default;

    virtual void visit(ASTVisitor&) = 0;
};

struct LiteralAST final : AST {
    std::variant<std::monostate, TokenKind, bool, char, std::int64_t, float, std::string> value;

    void visit(ASTVisitor& v) override;
};

struct IdAST final : AST {
    std::string name;

    void visit(ASTVisitor& v) override;
};

struct VarDeclAST final : AST {
    VarDecl var_decl;

    void visit(ASTVisitor& v) override;
};

struct BinAST final : AST {
    TokenKind op;

    std::unique_ptr<AST> lhs;
    std::unique_ptr<AST> rhs;

    void visit(ASTVisitor& v) override;
};

struct ReturnAST final : AST {
    std::unique_ptr<AST> value;

    void visit(ASTVisitor& v) override;
};

struct CallAST final : AST {
    std::unique_ptr<AST> callee;
    std::vector<std::unique_ptr<AST>> args;

    void visit(ASTVisitor& v) override;
};

struct BlockAST final : AST {
    std::vector<std::unique_ptr<AST>> statements;

    void visit(ASTVisitor& v) override;
};

struct FunctionAST final : AST {
    std::string name;
    std::vector<VarDecl> args;
    const TypeName* return_type = nullptr;

    std::unique_ptr<AST> body;

    void visit(ASTVisitor& v) override;
};

}  // namespace tiny

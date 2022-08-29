#include "ast.hpp"

#include "ast_visitor.hpp"

namespace tiny {

void LiteralAST::visit(ASTVisitor& v) { v.visit(*this); }

void IdAST::visit(ASTVisitor& v) { v.visit(*this); }

void VarDeclAST::visit(ASTVisitor& v) { v.visit(*this); }

void BinAST::visit(ASTVisitor& v) {
    lhs->visit(v);
    rhs->visit(v);

    v.visit(*this);
}

void ReturnAST::visit(ASTVisitor& v) {
    if (value) {
        value->visit(v);
    }

    v.visit(*this);
}

void CallAST::visit(ASTVisitor& v) {
    callee->visit(v);

    for (auto& arg : args) {
        arg->visit(v);
    }

    v.visit(*this);
}

void BlockAST::visit(ASTVisitor& v) {
    for (auto& statement : statements) {
        statement->visit(v);
    }
    v.visit(*this);
}

void FunctionAST::visit(ASTVisitor& v) {
    body->visit(v);
    v.visit(*this);
}

}  // namespace tiny

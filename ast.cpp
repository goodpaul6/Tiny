#include "ast.hpp"

#include "ast_visitor.hpp"

namespace tiny {

void LiteralAST::visit(ASTVisitor& v) { v.visit(*this); }

void IdAST::visit(ASTVisitor& v) { v.visit(*this); }

void BinAST::visit(ASTVisitor& v) { v.visit(*this); }

}  // namespace tiny

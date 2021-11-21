#include "ast_visitor.hpp"

namespace tiny {

void ASTVisitor::visit(LiteralAST&) {}
void ASTVisitor::visit(IdAST&) {}
void ASTVisitor::visit(VarDeclAST&) {}
void ASTVisitor::visit(BinAST&) {}

}  // namespace tiny

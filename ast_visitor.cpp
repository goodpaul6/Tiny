#include "ast_visitor.hpp"

namespace tiny {

void ASTVisitor::visit(LiteralAST&) {}
void ASTVisitor::visit(IdAST&) {}
void ASTVisitor::visit(VarDeclAST&) {}
void ASTVisitor::visit(BinAST&) {}
void ASTVisitor::visit(ReturnAST&) {}
void ASTVisitor::visit(CallAST&) {}
void ASTVisitor::visit(BlockAST&) {}
void ASTVisitor::visit(FunctionAST&) {}

}  // namespace tiny

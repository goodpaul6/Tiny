#pragma once

namespace tiny {

struct LiteralAST;
struct IdAST;
struct BinAST;

struct ASTVisitor {
    virtual void visit(LiteralAST&);
    virtual void visit(IdAST&);
    virtual void visit(BinAST&);
};

}  // namespace tiny

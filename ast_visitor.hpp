#pragma once

namespace tiny {

struct LiteralAST;
struct IdAST;
struct VarDeclAST;
struct BinAST;
struct ReturnAST;
struct CallAST;
struct BlockAST;
struct FunctionAST;

struct ASTVisitor {
    virtual void visit(LiteralAST&);
    virtual void visit(IdAST&);
    virtual void visit(VarDeclAST&);
    virtual void visit(BinAST&);
    virtual void visit(ReturnAST&);
    virtual void visit(CallAST&);
    virtual void pre_visit(BlockAST&);
    virtual void visit(BlockAST&);
    virtual void visit(FunctionAST&);
};

}  // namespace tiny

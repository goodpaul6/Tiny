#include "ast_interpreter.hpp"

#include <functional>
#include <stack>

#include "ast.hpp"
#include "ast_visitor.hpp"
#include "pos_error.hpp"

namespace {

struct Evaluator final : tiny::ASTVisitor {
    Evaluator(tiny::ASTInterpreter::Env& env) : m_env{env} {}

    void visit(tiny::LiteralAST& ast) override {
        std::visit(
            [&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, tiny::TokenType>) {
                    m_values.push(std::monostate{});
                } else {
                    m_values.push(value);
                }
            },
            ast.value);
    }

    void visit(tiny::IdAST& ast) override {
        auto found = m_env.find(ast.name);

        if (found == m_env.end()) {
            m_values.push(std::monostate{});
        } else {
            m_values.push(found->second);
        }
    }

    void visit(tiny::VarDeclAST& ast) override { m_values.push(std::monostate{}); }

    void visit(tiny::BinAST& ast) override {
        auto rhs_value = m_values.top();
        m_values.pop();

        auto lhs_value = m_values.top();
        m_values.pop();

        if (ast.op == tiny::TokenType::EQUAL) {
            if (auto* var_decl_ast = dynamic_cast<tiny::VarDeclAST*>(ast.lhs.get())) {
                auto found = m_env.find(var_decl_ast->name);

                if (found != m_env.end()) {
                    throw tiny::PosError{var_decl_ast->pos,
                                         "Multiple declaration of variable " + var_decl_ast->name};
                }

                m_env.try_emplace(var_decl_ast->name, std::move(rhs_value));
            } else if (auto* id_ast = dynamic_cast<tiny::IdAST*>(ast.lhs.get())) {
                auto found = m_env.find(id_ast->name);

                if (found == m_env.end()) {
                    throw tiny::PosError{
                        id_ast->pos, "Attempted to assign to undeclared variable " + id_ast->name};
                }

                found->second = std::move(rhs_value);
            } else {
                throw tiny::PosError{
                    ast.lhs->pos,
                    "Expected variable declaration or identifier on LHS of assignment"};
            }
        } else {
            m_values.push(std::visit(
                [&](auto&& a, auto&& b) -> tiny::ASTInterpreter::Value {
                    using A = std::decay_t<decltype(a)>;
                    using B = std::decay_t<decltype(b)>;

                    if constexpr (!std::is_same_v<A, B>) {
                        throw tiny::PosError{ast.pos, "Incompatible types in binary operation"};
                    } else if constexpr (!std::is_same_v<A, bool> && std::is_integral_v<A>) {
                        switch (ast.op) {
                            case tiny::TokenType::PLUS:
                                return std::plus<A>{}(a, b);
                            case tiny::TokenType::MINUS:
                                return std::minus<A>{}(a, b);
                            case tiny::TokenType::STAR:
                                return std::multiplies<A>{}(a, b);
                            case tiny::TokenType::SLASH:
                                return std::divides<A>{}(a, b);
                            default:
                                // TODO Implement other operators
                                return A{};
                        }
                    } else if constexpr (std::is_same_v<A, std::string>) {
                        return a + b;
                    }

                    throw tiny::PosError{ast.pos, "Cannot perform binary operation on these types"};
                },
                lhs_value, rhs_value));
        }
    }

    tiny::ASTInterpreter::Value last_value() const {
        return m_values.empty() ? std::monostate{} : m_values.top();
    }

private:
    tiny::ASTInterpreter::Env& m_env;
    std::stack<tiny::ASTInterpreter::Value> m_values;
};

}  // namespace

namespace tiny {

ASTInterpreter::Value ASTInterpreter::eval(AST& ast) {
    Evaluator eval{env};

    ast.visit(eval);

    return eval.last_value();
}

}  // namespace tiny

#include "ast_interpreter.hpp"

#include <cassert>
#include <functional>
#include <stack>

#include "ast.hpp"
#include "ast_visitor.hpp"
#include "function_view.hpp"
#include "pos_error.hpp"

namespace {

using EnvIter = tiny::ASTInterpreter::Env::iterator;

using EnvFindFn = tiny::FunctionView<EnvIter(const std::string&)>;
using EnvInsertFn = tiny::FunctionView<EnvIter(std::string, tiny::ASTInterpreter::Value)>;
using VoidFn = tiny::FunctionView<void()>;

struct FunctionEnvPopulator final : tiny::ASTVisitor {
    FunctionEnvPopulator(EnvInsertFn insert_fn) : m_insert_fn{insert_fn} {}

    void visit(tiny::FunctionAST& ast) { m_insert_fn(ast.name, &ast); }

private:
    EnvInsertFn m_insert_fn;
};

struct Evaluator final : tiny::ASTVisitor {
    using InsertFn = tiny::FunctionView<tiny::ASTInterpreter::Env::iterator(
        std::string, tiny::ASTInterpreter::Value)>;

    Evaluator(EnvFindFn find_fn, EnvInsertFn insert_fn, VoidFn push_env, VoidFn pop_env,
              EnvIter not_found_iter)
        : m_find_fn{std::move(find_fn)},
          m_insert_fn{insert_fn},
          m_push_env{push_env},
          m_pop_env{pop_env},
          m_not_found_iter{std::move(not_found_iter)} {}

    void visit(tiny::LiteralAST& ast) override {
        std::visit(
            [&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, tiny::TokenKind>) {
                    m_values.emplace_back();
                } else {
                    m_values.emplace_back(value);
                }
            },
            ast.value);
    }

    void visit(tiny::IdAST& ast) override {
        auto found = m_find_fn(ast.name);

        if (found == m_not_found_iter) {
            m_values.emplace_back();
        } else {
            m_values.emplace_back(found->second);
        }
    }

    void visit(tiny::VarDeclAST& ast) override { m_values.emplace_back(); }

    void visit(tiny::BinAST& ast) override {
        auto rhs_value = m_values.back();
        m_values.pop_back();

        auto lhs_value = m_values.back();
        m_values.pop_back();

        if (ast.op == tiny::TokenKind::EQUAL) {
            if (auto* var_decl_ast = dynamic_cast<tiny::VarDeclAST*>(ast.lhs.get())) {
                auto found = m_find_fn(var_decl_ast->var_decl.name);

                if (found != m_not_found_iter) {
                    throw tiny::PosError{var_decl_ast->pos, "Multiple declaration of variable " +
                                                                var_decl_ast->var_decl.name};
                }

                m_insert_fn(var_decl_ast->var_decl.name, std::move(rhs_value));
            } else if (auto* id_ast = dynamic_cast<tiny::IdAST*>(ast.lhs.get())) {
                auto found = m_find_fn(id_ast->name);

                if (found == m_not_found_iter) {
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
            m_values.emplace_back(std::visit(
                [&](auto&& a, auto&& b) -> tiny::ASTInterpreter::Value {
                    using A = std::decay_t<decltype(a)>;
                    using B = std::decay_t<decltype(b)>;

                    if constexpr (!std::is_same_v<A, B>) {
                        throw tiny::PosError{ast.pos, "Incompatible types in binary operation"};
                    } else if constexpr (!std::is_same_v<A, bool> && std::is_integral_v<A>) {
                        switch (ast.op) {
                            case tiny::TokenKind::PLUS:
                                return std::plus<A>{}(a, b);
                            case tiny::TokenKind::MINUS:
                                return std::minus<A>{}(a, b);
                            case tiny::TokenKind::STAR:
                                return std::multiplies<A>{}(a, b);
                            case tiny::TokenKind::SLASH:
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

    void visit(tiny::CallAST& ast) override {
        assert(m_values.size() >= ast.args.size() + 1);

        auto& func_value = m_values[m_values.size() - 1 - ast.args.size()];

        auto* func_ast_ptr = std::get_if<tiny::FunctionAST*>(&func_value);

        if (!func_ast_ptr) {
            throw tiny::PosError{ast.pos, "Attempted to call something that's not a function"};
        }

        auto* func_ast = *func_ast_ptr;

        // Create a new env an set arguments up
        m_push_env();

        for (auto arg_iter = func_ast->args.rbegin(); arg_iter != func_ast->args.rend();
             ++arg_iter) {
            m_insert_fn(arg_iter->name, m_values.back());
            m_values.pop_back();
        }

        m_pop_env();
    }

    tiny::ASTInterpreter::Value last_value() const {
        return m_values.empty() ? std::monostate{} : m_values.back();
    }

private:
    EnvFindFn m_find_fn;
    EnvInsertFn m_insert_fn;
    VoidFn m_push_env;
    VoidFn m_pop_env;
    EnvIter m_not_found_iter;

    std::vector<tiny::ASTInterpreter::Value> m_values;
};

}  // namespace

namespace tiny {

ASTInterpreter::Value ASTInterpreter::eval(AST& ast) { return {}; }

}  // namespace tiny

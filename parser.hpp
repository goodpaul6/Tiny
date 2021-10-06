#pragma once

#include <type_traits>

#include "ast.hpp"
#include "function_view.hpp"
#include "lexer.hpp"
#include "token_type.hpp"

namespace tiny {

struct Parser {
    using ASTPtr = std::unique_ptr<AST>;

    Parser(Lexer& lexer);

    void parse_until_eof(const FunctionView<void(ASTPtr)>& ast_handler);

private:
    Lexer& m_lex;

    TokenType m_cur_tok;

    TokenType next_token();

    // Throws an error if the current token is not equal to type
    void expect_token(TokenType type);

    // Throws an error if the current token is not equal to the type, or consumes
    // the token if it is
    void eat_token(TokenType type);

    ASTPtr parse_factor();
    ASTPtr parse_expr();
    ASTPtr parse_statement();

    template <typename T>
    std::unique_ptr<T> make_ast() {
        static_assert(std::is_base_of_v<AST, T>);

        auto ast = std::make_unique<T>();

        ast->pos = m_lex.pos();

        return ast;
    }
};

}  // namespace tiny

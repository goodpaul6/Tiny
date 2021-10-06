#include "parser.hpp"

#include "lexer.hpp"
#include "pos_error.hpp"

namespace {

bool is_operator(tiny::TokenType token) {
    using namespace tiny;

    return token == TokenType::EQUAL || token == TokenType::PLUS || token == TokenType::MINUS ||
           token == TokenType::STAR || token == TokenType::SLASH;
}

}  // namespace

namespace tiny {

Parser::Parser(Lexer& lexer) : m_lex{lexer} {}

void Parser::parse_until_eof(const FunctionView<void(ASTPtr)>& ast_handler) {
    next_token();

    while (m_cur_tok != TokenType::SUB) {
        ast_handler(parse_statement());
    }
}

TokenType Parser::next_token() { return (m_cur_tok = m_lex.next_token()); }

void Parser::expect_token(TokenType type) {
    if (m_cur_tok != type) {
        throw PosError{m_lex.pos(), "Unexpected token"};
    }
}

void Parser::eat_token(TokenType type) {
    expect_token(type);
    next_token();
}

Parser::ASTPtr Parser::parse_factor() {
    switch (m_cur_tok) {
        case TokenType::IDENT: {
            auto ast = make_ast<IdAST>();

            ast->name = m_lex.str();
            next_token();

            return ast;
        } break;

        case TokenType::INT_VALUE: {
            auto ast = make_ast<LiteralAST>();

            ast->value = m_lex.i_value();
            next_token();

            return ast;
        } break;

        case TokenType::FLOAT_VALUE: {
            auto ast = make_ast<LiteralAST>();

            ast->value = m_lex.f_value();
            next_token();

            return ast;
        } break;

        default:
            break;
    }

    throw PosError{m_lex.pos(), "Expected factor"};
}

Parser::ASTPtr Parser::parse_expr() {
    auto ast = parse_factor();

    while (is_operator(m_cur_tok)) {
        auto bin_ast = make_ast<BinAST>();

        bin_ast->op = m_cur_tok;
        next_token();

        bin_ast->lhs = std::move(ast);
        bin_ast->rhs = parse_factor();

        ast = std::move(bin_ast);
    }

    return ast;
}

Parser::ASTPtr Parser::parse_statement() { return parse_expr(); }

}  // namespace tiny

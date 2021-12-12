#include "parser.hpp"

#include <array>
#include <cassert>
#include <optional>

#include "lexer.hpp"
#include "pos_error.hpp"
#include "primitive_type.hpp"

namespace {

struct Entity {
    std::string_view str;
    tiny::PrimitiveType primitive_type;
};

constexpr std::array<Entity, static_cast<size_t>(tiny::PrimitiveType::COUNT)> PRIMITIVE_TYPES{
    {{"void", tiny::PrimitiveType::VOID},
     {"bool", tiny::PrimitiveType::BOOL},
     {"char", tiny::PrimitiveType::CHAR},
     {"int", tiny::PrimitiveType::INT},
     {"float", tiny::PrimitiveType::FLOAT},
     {"str", tiny::PrimitiveType::STR},
     {"any", tiny::PrimitiveType::ANY}}};

bool is_operator(tiny::TokenKind token) {
    using namespace tiny;

    return token == TokenKind::EQUAL || token == TokenKind::PLUS || token == TokenKind::MINUS ||
           token == TokenKind::STAR || token == TokenKind::SLASH;
}

}  // namespace

namespace tiny {

Parser::Parser(Lexer& lexer, TypeNamePool& type_name_pool)
    : m_lex{lexer}, m_type_name_pool{type_name_pool} {}

void Parser::parse_until_eof(const FunctionView<void(ASTPtr)>& ast_handler) {
    next_token();

    while (m_cur_tok != TokenKind::SUB) {
        ast_handler(parse_statement());
    }
}

TokenKind Parser::next_token() { return (m_cur_tok = m_lex.next_token()); }

void Parser::expect_token(TokenKind type) {
    if (m_cur_tok != type) {
        throw PosError{m_lex.pos(), "Unexpected token"};
    }
}

void Parser::eat_token(TokenKind type) {
    expect_token(type);
    next_token();
}

const TypeName& Parser::parse_type() {
    const TypeName* type = nullptr;

    if (m_cur_tok == TokenKind::OPENSQUARE) {
        // Parse a map
        next_token();

        const auto& key = parse_type();

        eat_token(TokenKind::CLOSESQUARE);

        const auto& value = parse_type();

        type = &m_type_name_pool.map(key, value);
    } else {
        // We parse types as identifiers since it prevents us from
        // reserving their names as keywords, even for primitive types.
        expect_token(TokenKind::IDENT);

        std::optional<PrimitiveType> primitive_type;

        for (const auto& entity : PRIMITIVE_TYPES) {
            if (m_lex.str() == entity.str) {
                primitive_type = entity.primitive_type;
                break;
            }
        }

        if (!primitive_type) {
            throw PosError{m_lex.pos(), "Identifier does not denote a primitive type"};
        }

        next_token();

        type = &m_type_name_pool.primitive_type(*primitive_type);
    }

    assert(type);

    while (m_cur_tok == TokenKind::OPENSQUARE) {
        next_token();

        eat_token(TokenKind::CLOSESQUARE);

        type = &m_type_name_pool.array(*type);
    }

    return *type;
}

Parser::ASTPtr Parser::parse_factor() {
    switch (m_cur_tok) {
        case TokenKind::IDENT: {
            auto ast = make_ast<IdAST>();

            ast->name = m_lex.str();
            next_token();

            return ast;
        } break;

        case TokenKind::INT_VALUE: {
            auto ast = make_ast<LiteralAST>();

            ast->value = m_lex.i_value();
            next_token();

            return ast;
        } break;

        case TokenKind::FLOAT_VALUE: {
            auto ast = make_ast<LiteralAST>();

            ast->value = m_lex.f_value();
            next_token();

            return ast;
        } break;

        case TokenKind::STRING_VALUE: {
            auto ast = make_ast<LiteralAST>();

            ast->value = m_lex.str();
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

Parser::ASTPtr Parser::parse_statement() {
    switch (m_cur_tok) {
        case TokenKind::IDENT: {
            auto str = m_lex.str();

            next_token();

            eat_token(TokenKind::COLON);

            const auto& type = parse_type();

            auto var_ast = make_ast<VarDeclAST>();

            var_ast->name = std::move(str);
            var_ast->type = &type;

            eat_token(TokenKind::EQUAL);

            auto bin_ast = make_ast<BinAST>();

            bin_ast->op = TokenKind::EQUAL;
            bin_ast->lhs = std::move(var_ast);
            bin_ast->rhs = parse_expr();

            return bin_ast;
        } break;

        default: {
            throw PosError{m_lex.pos(), "Expected identifier at start of statement."};
        } break;
    }
}

}  // namespace tiny

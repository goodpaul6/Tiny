#include "lexer.hpp"

#include <array>
#include <istream>
#include <string_view>

#include "pos_error.hpp"

namespace {

struct Entity {
    std::string_view str;
    tiny::TokenKind token_type;
};

constexpr std::array<Entity, 8> SEPARATORS{{{"(", tiny::TokenKind::OPENPAREN},
                                            {")", tiny::TokenKind::CLOSEPAREN},
                                            {"{", tiny::TokenKind::OPENCURLY},
                                            {"}", tiny::TokenKind::CLOSECURLY},
                                            {",", tiny::TokenKind::COMMA},
                                            {";", tiny::TokenKind::SEMI},
                                            {"[", tiny::TokenKind::OPENSQUARE},
                                            {"]", tiny::TokenKind::CLOSESQUARE}}};

constexpr std::array<Entity, 12> OPERATORS{{{"=", tiny::TokenKind::EQUAL},

                                            {"+", tiny::TokenKind::PLUS},
                                            {"-", tiny::TokenKind::MINUS},
                                            {"*", tiny::TokenKind::STAR},
                                            {"/", tiny::TokenKind::SLASH},
                                            {":", tiny::TokenKind::COLON},

                                            {"+=", tiny::TokenKind::PLUS_EQUAL},
                                            {"-=", tiny::TokenKind::MINUS_EQUAL},
                                            {"*=", tiny::TokenKind::STAR_EQUAL},
                                            {"/=", tiny::TokenKind::SLASH_EQUAL},

                                            {":=", tiny::TokenKind::DECLARE},
                                            {"::", tiny::TokenKind::DECLARE_CONST}}};

constexpr std::array<Entity, 11> KEYWORDS{{{"var", tiny::TokenKind::VAR},
                                           {"if", tiny::TokenKind::IF},
                                           {"else", tiny::TokenKind::ELSE},
                                           {"while", tiny::TokenKind::WHILE},
                                           {"for", tiny::TokenKind::FOR},
                                           {"return", tiny::TokenKind::RETURN},
                                           {"func", tiny::TokenKind::FUNC},
                                           {"struct", tiny::TokenKind::STRUCT},
                                           {"new", tiny::TokenKind::NEW},
                                           {"cast", tiny::TokenKind::CAST},
                                           {"null", tiny::TokenKind::NULL_VALUE}}};

constexpr bool is_whitespace(int ch) {
    return ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' || ch == '\f';
}

constexpr bool is_letter(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$';
}

constexpr bool is_digit(int ch) { return ch >= '0' && ch <= '9'; }

constexpr bool is_letter_or_digit(int ch) { return is_letter(ch) || is_digit(ch); }

constexpr bool is_sub(int ch) { return ch == std::istream::traits_type::eof(); }

}  // namespace

namespace tiny {

Lexer::Lexer(std::istream& s, std::string filename) : m_s{s}, m_pos{1, filename} {}

TokenKind Lexer::next_token() {
    m_str.clear();

    const auto push_get = [this] {
        m_str.push_back(m_last);
        m_last = m_s.get();
    };

    while (is_whitespace(m_last)) {
        m_last = m_s.get();
    }

    if (is_letter(m_last)) {
        while (is_letter_or_digit(m_last)) {
            push_get();
        }

        for (const auto& kw : KEYWORDS) {
            if (m_str == kw.str) {
                return kw.token_type;
            }
        }

        if (m_str == "true" || m_str == "false") {
            m_b_value = m_str == "true";
            return TokenKind::BOOL_VALUE;
        }

        return TokenKind::IDENT;
    }

    if (is_digit(m_last)) {
        bool has_radix = false;

        while (is_digit(m_last) || m_last == '.') {
            has_radix = has_radix || m_last == '.';

            push_get();
        }

        if (has_radix) {
            m_f_value = std::stof(m_str);
            return TokenKind::FLOAT_VALUE;
        }

        m_i_value = std::stoll(m_str);
        return TokenKind::INT_VALUE;
    }

    if (m_last == '\'') {
        m_last = m_s.get();
        // TODO Escape sequences
        m_c_value = m_last;
        m_last = m_s.get();

        if (m_last != '\'') {
            throw PosError{m_pos, "Expected ' to terminate character literal"};
        }

        m_last = m_s.get();

        return TokenKind::CHAR_VALUE;
    }

    if (m_last == '"') {
        m_last = m_s.get();

        while (!is_sub(m_last) && m_last != '"') {
            push_get();
        }

        if (m_last != '"') {
            throw PosError{m_pos, "Expected \" to terminate string literal"};
        }

        m_last = m_s.get();

        return TokenKind::STRING_VALUE;
    }

    for (const auto& sep : SEPARATORS) {
        if (m_last == sep.str[0]) {
            m_last = m_s.get();
            return sep.token_type;
        }
    }

    if (is_sub(m_last)) {
        return TokenKind::SUB;
    }

    // We look for the longest operator that matches
    // the characters consumed so far exactly.
    const Entity* last_valid_operator = nullptr;

    for (;;) {
        // We stop as soon as there are no possible operators
        // that match the currently consumed sequence.
        const Entity* potential_operator = nullptr;

        m_str.push_back(m_last);

        for (const auto& op : OPERATORS) {
            if (op.str.substr(0, m_str.size()) == m_str) {
                potential_operator = &op;

                if (m_str == op.str) {
                    last_valid_operator = &op;
                }
            }
        }

        if (!potential_operator) {
            break;
        }

        m_last = m_s.get();
    }

    if (last_valid_operator) {
        return last_valid_operator->token_type;
    }

    throw PosError{m_pos, "Unexpected character(s) " + m_str};
}

const std::string& Lexer::str() const { return m_str; }

bool Lexer::b_value() const { return m_b_value; }
char Lexer::c_value() const { return m_c_value; }
std::int64_t Lexer::i_value() const { return m_i_value; }
float Lexer::f_value() const { return m_f_value; }

Pos Lexer::pos() const { return m_pos; }

}  // namespace tiny

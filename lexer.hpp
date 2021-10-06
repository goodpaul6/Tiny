#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "pos.hpp"
#include "token_type.hpp"

namespace tiny {

struct Lexer {
    Lexer(std::istream& s, std::string filename);

    TokenType next_token();

    const std::string& str() const;

    bool b_value() const;
    char c_value() const;
    std::int64_t i_value() const;
    float f_value() const;

    Pos pos() const;

private:
    std::istream& m_s;

    Pos m_pos;

    // This intiial empty space makes sure that the lexer
    // reads at least one character from the stream
    // since it is automatically skipped.
    int m_last = ' ';

    // The string buffer of the token
    std::string m_str;

    // For literals, we store their values here for convenience.
    // Note than only one of these is valid at any given time.
    union {
        bool m_b_value;
        char m_c_value;
        std::int64_t m_i_value;
        float m_f_value;
    };
};

}  // namespace tiny

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include "ast.hpp"
#include "ast_interpreter.hpp"
#include "parser.hpp"
#include "pos_error.hpp"
#include "type_name_pool.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    // As it stands, the only type of statement we have is a variable declaration,
    // so we test arithmetic in terms of that.
    std::string str{"x: int = 10 y: int = x * 10 + 20 z: string = \"hello \" + \"world\""};
    std::istringstream ss{str};

    Lexer lexer{ss, "test"};
    TypeNamePool type_name_pool;
    Parser parser{lexer, type_name_pool};
    ASTInterpreter ast_interpreter;

    try {
        parser.parse_until_eof([&](auto ast) { ast_interpreter.eval(*ast); });
    } catch (const PosError&) {
        assert(false);
    }

    assert(std::get<std::int64_t>(ast_interpreter.env.at("y")) == 120);
    assert(std::get<std::string>(ast_interpreter.env.at("z")) == "hello world");

    return 0;
}

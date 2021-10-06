#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include "ast.hpp"
#include "ast_interpreter.hpp"
#include "parser.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    std::string str{"x = 10 x * 10 + 20"};
    std::istringstream ss{str};

    Lexer lexer{ss, "test"};
    Parser parser{lexer};
    ASTInterpreter interpreter;

    ASTInterpreter::Value last_value;

    parser.parse_until_eof([&](auto ast) { last_value = interpreter.eval(*ast); });

    assert(std::get<std::int64_t>(last_value) == 120);

    return 0;
}

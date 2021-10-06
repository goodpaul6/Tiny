#include <cassert>
#include <sstream>
#include <string>

#include "ast.hpp"
#include "parser.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    std::string str{"x * 10 + 20"};
    std::istringstream ss{str};

    Lexer lexer{ss, "test"};
    Parser parser{lexer};

    parser.parse_until_eof([](auto ast) { assert(dynamic_cast<const BinAST*>(ast.get())); });

    return 0;
}

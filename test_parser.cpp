#include <cassert>
#include <sstream>
#include <string>

#include "ast.hpp"
#include "parser.hpp"
#include "type_name_pool.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    std::string str{"var x [][][]map[int]string = x * 10 + 20"};
    std::istringstream ss{str};

    Lexer lexer{ss, "test"};
    TypeNamePool type_name_pool;
    Parser parser{lexer, type_name_pool};

    parser.parse_until_eof([](auto ast) { assert(dynamic_cast<const BinAST*>(ast.get())); });

    return 0;
}

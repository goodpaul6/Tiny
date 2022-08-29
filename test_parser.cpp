#include <cassert>
#include <sstream>
#include <string>

#include "ast.hpp"
#include "parser.hpp"
#include "type_name_pool.hpp"

int main(int argc, char** argv) {
    using namespace tiny;

    std::string str{
        "func add(x int, y int) int { "
        "	var x [][][]map[int]string = x * 10 + 20 "
        "	foo(x + y, x, y)"
        "	return x + y"
        "}"};
    std::istringstream ss{str};

    Lexer lexer{ss, "test"};
    TypeNamePool type_name_pool;
    Parser parser{lexer, type_name_pool};

    parser.parse_until_eof([](auto ast) {
        auto* func_ast = dynamic_cast<const FunctionAST*>(ast.get());
        assert(func_ast);

        auto* block_ast = dynamic_cast<const BlockAST*>(func_ast->body.get());
        assert(block_ast);

        assert(block_ast->statements.size() == 3);

        auto* bin_ast = dynamic_cast<const BinAST*>(block_ast->statements[0].get());
        assert(bin_ast);

        auto* call_ast = dynamic_cast<const CallAST*>(block_ast->statements[1].get());
        assert(call_ast);

        auto* return_ast = dynamic_cast<const ReturnAST*>(block_ast->statements[2].get());
        assert(return_ast);
    });

    return 0;
}

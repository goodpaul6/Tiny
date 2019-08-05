// AST

typedef enum ASTType
{
    AST_ID,
    AST_CALL,
    AST_NULL,
    AST_BOOL,
    AST_CHAR,
    AST_INT,
    AST_FLOAT,
    AST_STRING,
    AST_BINARY,
    AST_PAREN,
    AST_BLOCK,
    AST_PROC,
    AST_IF,
    AST_UNARY,
    AST_RETURN,
    AST_WHILE,
    AST_FOR,
    AST_DOT,
    AST_CONSTRUCTOR,
    AST_CAST
} ASTType;

typedef struct AST
{
    ASTType type;
	TokenPos pos;

    Typetag* tag;

    union
    {
        bool boolean;

        int iValue;
        int fIndex;

        const char* str;

        struct
        {
            const char* name;
            Sym* sym;
        } id;

        struct
        {
            const char* calleeName;

            // Buffer
            struct AST** args; 
        } call;
        
        struct
        {
            struct AST* lhs;
            struct AST* rhs;
            int op;
        } binary;
        
        struct AST* paren;
        
        struct 
        {
            int op;
            struct AST* exp;
        } unary;
        
        // Buffer
        struct AST** block;

        struct
        {
            Sym* decl;
            struct AST* body;
        } proc;

        struct
        {
            struct AST* cond;
            struct AST* body;
            struct AST* alt;
        } ifx;
        
        struct
        {
            struct AST* cond;
            struct AST* body;
        } whilex;

        struct
        {
            struct AST* init;
            struct AST* cond;
            struct AST* step;
            struct AST* body;
        } forx;
        
        struct
        {
            struct AST* lhs;
            const char* field;
        } dot;

        struct
        {
            Sym* structSym;
            struct AST** args;
        } constructor;

        struct
        {
            struct AST* value;
            Typetag* tag;
        } cast;

        struct AST* retExpr;
    };
} AST;

typedef enum ASTVisitOrder
{
    AST_VISIT_PRE,
    AST_VISIT_POST
} ASTVisitOrder;

typedef void (*ASTVisitorFunction)(void* data, AST* ast);

static void VisitAST(AST* ast, ASTVisitOrder order, ASTVisitorFunction func, void* data)
{
    if(!ast) {
        return;
    }

    if(order == AST_VISIT_PRE) {
        func(data, ast);
    }

    switch(ast->type) {
        case AST_NULL:
        case AST_BOOL:
        case AST_CHAR:
        case AST_INT:
        case AST_FLOAT:
        case AST_STRING:
        case AST_ID:
            break;

        case AST_CALL: {
            for(int i = 0; i < BUF_LEN(ast->call.args); ++i) {
                VisitAST(ast->call.args[i], order, func, data);
            }
        } break;

        case AST_PAREN: {
            VisitAST(ast->paren, order, func, data);
        } break;

        case AST_BINARY: {
            VisitAST(ast->binary.lhs, order, func, data);
            VisitAST(ast->binary.rhs, order, func, data);
        } break;

        case AST_UNARY: {
            VisitAST(ast->unary.exp, order, func, data);
        } break;

        case AST_BLOCK: {
            for(int i = 0; i < BUF_LEN(ast->block); ++i) {
                VisitAST(ast->block[i], order, func, data);
            }
        } break;

        case AST_PROC: {
            VisitAST(ast->proc.body, order, func, data);
        } break;

        case AST_IF: {
            VisitAST(ast->ifx.cond, order, func, data);
            VisitAST(ast->ifx.body, order, func, data);
            VisitAST(ast->ifx.alt, order, func, data);
        } break;

        case AST_RETURN: {
            VisitAST(ast->retExpr, order, func, data);
        } break;

        case AST_WHILE: {
            VisitAST(ast->whilex.cond, order, func, data);
            VisitAST(ast->whilex.body, order, func, data);
        } break;

        case AST_FOR: {
            VisitAST(ast->forx.init, order, func, data);
            VisitAST(ast->forx.cond, order, func, data);
            VisitAST(ast->forx.step, order, func, data);
            VisitAST(ast->forx.body, order, func, data);
        } break;

        case AST_DOT: {
            VisitAST(ast->dot.lhs, order, func, data);
        } break;

        case AST_CONSTRUCTOR: {
            for(int i = 0; i < BUF_LEN(ast->constructor.args); ++i) {
                VisitAST(ast->constructor.args[i], order, func, data);
            }
        } break;

        case AST_CAST: {
            VisitAST(ast->cast.value, order, func, data);
        } break;
    }

    if(order == AST_VISIT_POST) {
        func(data, ast);
    }
}

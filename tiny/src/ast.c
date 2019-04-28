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

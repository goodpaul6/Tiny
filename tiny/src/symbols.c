
typedef enum SymType
{
    SYM_VAR,
    SYM_CONST,
    SYM_FUNC,
    SYM_FOREIGN_FUNC,
    SYM_TYPE
} SymType;

typedef struct Sym
{
    SymType type;
    const char* name;


} Sym;

#ifndef sql_ast
#define sql_ast

#include <stdint.h>

enum AstNodeType {
    AST_SELECT,
    AST_FROM,
    AST_EXPR,
};

// @TODO: names should hold pointers to command string and structs
// should also hold lengths of those names

// Expressions

enum ExprType {
    EXPR_INTEGER,
    EXPR_STRING,
    EXPR_COLUMN,
    EXPR_FUNCTION,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_STAR
};

struct Integer {
    int64_t    value;
};

struct String {
    char    *start;
    size_t  len;
};

struct Column {
    char    *name;
};

enum AggType {
    AGG_COUNT
};

struct ExprList {
    size_t          count;
    size_t          capacity;
    struct Expr     **list;
};

struct Function {
    enum AggType    agg_type;
    char            *name;
    struct ExprList *args;
};

enum BinaryOp {
    BIN_EQUAL,
    BIN_LESS,
    BIN_GREATER,
};

struct Binary {
    struct Expr     *left;
    struct Expr     *right;
    enum BinaryOp   op;
};

struct Unary {
    struct Expr     *right;
    int             op;
};

struct Star {
    int     tag;
};

struct Expr {
    enum ExprType type;

    union {
        struct Integer      integer;
        struct String       string;
        struct Column       column;
        struct Function     function;
        struct Binary       binary;
        struct Unary        unary;
    };
};

// Select Statement

struct SelectStatement {
    char            *from_table;
    struct ExprList *select_list;
    struct ExprList *where_list;
};

// From statement
struct FromStatement {
    char *table;
};

// Create Index Statement
struct CreateIndexStatement {
    char            *index_name;
    size_t           index_name_len;
    char            *on_table;
    struct Columns  *indexed_columns;
};


void print_expr_star_to_stderr(int padding);
void print_expr_column_to_stderr(struct Expr *expr, int padding);
void print_expr_function_to_stderr(struct Expr *expr, int padding);
void print_expr_binary_to_stderr(struct Expr *expr, int padding);
void print_expression_to_stderr(struct Expr *expr, int padding);
void print_expression_list_to_stderr(struct ExprList *expr_list, int padding);
void print_select_statement_to_stderr(struct SelectStatement *stmt, int padding);

#endif
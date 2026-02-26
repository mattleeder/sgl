#ifndef sql_ast
#define sql_ast

#include <stdint.h>

#include "common.h"
#include "memory.h"
#include "sql_utils.h"
#include "./utilities/hash_map.h"

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

struct ExprInteger {
    int64_t    value;
};

struct ExprString {
    char    *start;
    size_t  len;
};

struct ExprColumn {
    size_t                      idx; // @TODO: not sure if this should be here
    struct UnterminatedString   name;
};

enum AggType {
    AGG_COUNT
};

struct ExprFunction {
    enum AggType    agg_type;
    char            *name;
    struct ExprList *args;
};

enum BinaryOp {
    BIN_EQUAL,
    BIN_LESS,
    BIN_GREATER,
};

struct ExprBinary {
    struct Expr     *left;
    struct Expr     *right;
    enum BinaryOp   op;
};

struct ExprUnary {
    struct Expr     *right;
    int             op;
};

struct ExprStar {
    int     tag;
};

struct Expr {
    enum ExprType               type;
    struct UnterminatedString   text;

    union {
        struct ExprInteger      integer;
        struct ExprString       string;
        struct ExprColumn       column;
        struct ExprFunction     function;
        struct ExprBinary       binary;
        struct ExprUnary        unary;
    };
};

DEFINE_VECTOR(struct Expr, ExprList, expr_list)
DEFINE_VECTOR(struct ExprBinary, BinaryExprList, binary_expr_list)

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
void print_expr_column_to_stderr(struct ExprColumn *column, int padding);
void print_expr_function_to_stderr(struct ExprFunction *function, int padding);
void print_expr_binary_to_stderr(struct ExprBinary *binary, int padding);
void print_expression_to_stderr(struct Expr *expr, int padding);
void print_expression_list_to_stderr(struct ExprList *expr_list, int padding);
void print_select_statement_to_stderr(struct SelectStatement *stmt, int padding);

void print_binary_expr_list_to_stderr(struct BinaryExprList *expr_list, int padding);

void get_column_from_expression(struct Expr *expr, struct HashMap *columns);
struct HashMap *get_columns_from_expression_list(struct ExprList *expr_list);
struct IndexComparisonArray *get_index_comparisons(struct ExprList *expr_list);

#endif
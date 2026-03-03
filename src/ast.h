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
    struct UnterminatedString string;
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
enum StatementType {
    STMT_SELECT
};

struct SQLStatement {
    enum StatementType type;
    bool explain;
    
};

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
    struct UnterminatedString   index_name;
    char                        *on_table;
    struct Columns              *indexed_columns;
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

enum LiteralType {
    LITERAL_NUMBER,
    LITERAL_STRING,
    LITERAL_NULL,
    LITERAL_CURRENT_TIME,
    LITERAL_CURRENT_DATE,
    LITERAL_CURRENT_TIMESTAMP,
    LITERAL_BOOLEAN,
    LITERAL_BLOB
};

struct LiteralNumber {
    int64_t value;
};

struct LiteralString {
    struct UnterminatedString value;
};

struct LiteralBoolean {
    bool    value;
};

struct LiteralBlob {
    uint8_t *value;
    size_t  length;
};

struct NewExprLiteral {
    enum LiteralType            type;
    struct UnterminatedString   text;

    union {
        struct LiteralNumber            number;
        struct LiteralString            string;
        struct LiteralBoolean           boolean;
        struct LiteralBlob              blob;
    };
};

struct NewExprBind {
    bool tag;
};

struct NewExprName {
    struct UnterminatedString name;
};

enum SortType {
    SORT_NONE,
    SORT_ASC,
    SORT_DESC
};

enum CollationType {
    COLLATE_NONE
};

enum NullsPosition {
    NULLS_NONE,
    NULLS_FIRST,
    NULLS_LAST
};

struct OrderByClause {
    enum SortType       sort_type;
    enum CollateType    collate_type;
    enum NullsPosition  nulls_position;
    struct NewExpr *expr;
};

struct FilterClause {
    struct NewExpr *expr;
};

enum FrameType { 
    FRAME_RANGE,
    FRAME_ROWS,
    FRAME_GROUPS 
};

enum FrameBoundType { 
    FB_UNBOUNDED_PRECEDING,
    FB_CURRENT_ROW,
    FB_N_PRECEDING,
    FB_N_FOLLOWING,
    FB_UNBOUNDED_FOLLOWING 
};

enum FrameExclude { 
    EXCLUDE_NO_OTHERS, 
    EXCLUDE_CURRENT_ROW, 
    EXCLUDE_GROUP, 
    EXCLUDE_TIES 
};

struct FrameBound {
    enum FrameBoundType type;
    struct NewExpr  *offset; // If FB_N_PRECEDING or FB_N_FOLLOWING
};

struct FrameSpec {
    enum FrameType      type;
    struct FrameBound   start;
    struct FrameBound   end;             // default FB_CURRENT_ROW
    enum FrameExclude   exclude;         // default EXCLUDE_NO_OTHERS
};

struct OverClause {
    // Simple form
    struct UnterminatedString *referenced_window;

    // Inline form: OVER ( ... )
    struct UnterminatedString   *base_window;
    struct NewExprList          *partition_by;
    struct OrderByClauseList    *order_by;
    struct FrameSpec            *frame_spec;
};

struct NewExprFunctionArgs {
    bool distinct;
    bool star;

    struct NewExprList      *exprs;
    struct OrderByClause    *order_by;
};

struct NewExprFunction {
    struct UnterminatedString   name;
    struct NewExprFunctionArgs  *args;

    struct FilterClause         *filter;
    struct OverClause           *over;
    
    struct UnterminatedString   text;
};

enum CastType {
    TYPE_BLOB,
    TYPE_INTEGER,
    TYPE_NULL,
    TYPE_REAL,
    TYPE_TEXT
};

struct NewExprCast {
    struct NewExpr *expr;
    enum CastType   cast_type;
};

struct NewExprSubQuery {
    struct SelectStatement *subquery;
};

struct NewExprExists {
    struct SelectStatement *subquery;
};

struct CaseClause {
    struct NewExpr *when;
    struct NewExpr *then;
};

struct NewExprCase {
    struct NewExpr          *first_expr;
    struct CaseClauseList   *clauses;
    struct NewExpr          *else_expr;
};

struct NewExprRaise {
    bool tag;
};


enum NewExprType {
    EXPR_LITERAL,
    EXPR_BIND,
    EXPR_NAME,
    EXPR_FUNCTION,
    EXPR_CAST,
    EXPR_SUBQUERY,
    EXPR_EXISTS,
    EXPR_CASE,
    EXPR_RAISE
};

struct PrimaryExpr {
    enum ExprType               type;
    struct UnterminatedString   text;

    union {
        struct NewExprLiteral   *literal;
        struct NewExprBind      *bind;
        struct NewExprName      *name;
        struct NewExprFunction  *function;
        struct NewExprCast      *cast;
        struct NewExprSubQuery  *subquery;
        struct NewExprExists    *exists;
        struct NewExprCase      *expr_case;
        struct NewExprRaise     *raise;
    };
};

struct NewExpr {
    bool tag;
};

DEFINE_VECTOR(struct NewExpr *, NewExprPtrList, new_expr_ptr_list)
DEFINE_VECTOR(struct CaseClause, CaseClauseList, case_clause_list)
DEFINE_VECTOR(struct OrderByClause *, OrderByClausePtrList, order_by_clause_ptr_list)

#endif
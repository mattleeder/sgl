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

struct NewExprPtrList;

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
    enum CollationType  collate_type;
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

struct WindowDefinition {
    struct UnterminatedString       *base_window;
    struct NewExprPtrList           *partition_by;
    struct OrderByClausePtrList     *order_by;
    struct FrameSpec                *frame_spec;
};

enum OverClauseType {
    OVER_REFERENCED,
    OVER_INLINE
};

struct OverClause {
    enum OverClauseType type;
    
    union {
        struct UnterminatedString   *referenced_window;
        struct WindowDefinition     *inline_window;
    };
};

struct NewExprFunctionArgs {
    bool distinct;
    bool star;

    struct NewExprPtrList           *exprs;
    struct OrderByClausePtrList     *order_by;
};

struct NewExprFunction {
    struct UnterminatedString   name;
    struct NewExprFunctionArgs  *args;

    struct FilterClause         *filter;
    struct OverClause           *over;
    
    struct UnterminatedString   text;
};

#define QUALIFIED_NAME_PARTS (3)
struct QualifiedName {
    size_t count;
    struct UnterminatedString parts[QUALIFIED_NAME_PARTS];
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

struct NewExprRowValue {
    struct NewExprPtrList *elements;
};

struct NewExprGrouping {
    struct NewExpr *inner;
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

enum RaiseType {
    RAISE_IGNORE,
    RAISE_ROLLBACK,
    RAISE_ABORT,
    RAISE_FAIL
};

struct NewExprRaise {
    enum RaiseType type;
    struct NewExpr *expr; // If RAISE_ROLLBACK
};

enum ResultColumnType {
    RC_EXPR,
    RC_ALL,
    RC_TABLE_ALL
};

struct ResultColumn {
    enum ResultColumnType       type;

    union {
        struct { // If RC_EXPR
            struct NewExpr              *expr;
            struct UnterminatedString   *alias; // Optional
        } expr;
        
        struct { // If RC_TABLE_ALL
            struct UnterminatedString   table_name;
        } table_all;

        // RC_ALL has no additional data
    };
};

enum TableIndexMode {
    TABLE_INDEX_AUTO,
    TABLE_INDEX_NAMED,
    TABLE_INDEX_NONE
};

struct TableName {
    enum TableIndexMode         index_mode;
    struct UnterminatedString   table_name;
    struct UnterminatedString   *schema_name; // Optional
    struct UnterminatedString   *index_name;  // Optional
};

struct TableFunction {
    struct UnterminatedString   function_name;
    struct NewExprPtrList       *args;
    struct UnterminatedString   *schema_name; // Optional
};

enum TableOrSubqueryType {
    TOS_TABLE_NAME,
    TOS_TABLE_FUNCTION,
    TOS_SUBQUERY
};


struct TableOrSubquery {
    enum TableOrSubqueryType    type;
    struct UnterminatedString   *alias;

    union {
        struct TableName            *table_name;
        struct TableFunction        *table_function;
        struct SelectStatementNew   *subquery;
    };

};

enum JoinConstraintType {
    JOIN_CONSTRAINT_ON,
    JOIN_CONSTRAINT_USING
};

struct JoinConstraint {
    enum JoinConstraintType type;

    union {
        struct NewExpr                  *expr;
        struct UnterminatedStringList   *column_names;
    };

};

enum JoinOperatorType {
    JO_CROSS,
    JO_INNER,
    JO_LEFT_OUTER,
    JO_RIGHT_OUTER,
    JO_FULL_OUTER
};

struct JoinData {
    bool natural;   // If true then constrain must be NULL

    enum JoinOperatorType   join_operator;
    struct TableExpression  *right;
    struct JoinConstraint   *constraint;
};

struct JoinClause {
    struct TableExpression  *left;
    struct JoinDataPtrList  *joins;
};

enum TableExprType {
    TE_SIMPLE,
    TE_JOIN
};

struct TableExpression {
    enum TableExprType type;

    union {
        struct TableOrSubquery          *simple;
        struct JoinClause               *join;
    };

};

struct FromClause {
    struct JoinClause *tables; // Join clause can have standalone table
};

struct WhereClause {
    struct NewExpr *expr;
};

struct GroupByClause {
    struct NewExprPtrList *expr_ptr_list;
};

struct HavingClause {
    struct NewExpr *expr;
};

struct WindowData {
    struct UnterminatedString   name;
    struct WindowDefinition     *definition;
};

struct WindowClause {
    struct WindowDataPtrList *window_list;
};

enum SelectCoreType {
    SC_SELECT,
    SC_VALUES
};

struct SelectCore {
    enum SelectCoreType type;

    union {

        struct {
            struct NewExprPtrListPtrList *values;
        } values;

        struct {
            bool distinct;
            struct ResultColumnPtrList *result_columns;
            struct FromClause       *from;
            struct WhereClause      *where;
            struct GroupByClause    *group_by;
            struct HavingClause     *having;
            struct WindowClause     *window;
        } select;

    };
};

struct LimitClause {
    struct NewExpr *limit;
    struct NewExpr *offset; // Optional
};

enum CompoundOperatorType {
    COMPOUND_OPERATOR_BASE,     // Used for initial core / when there is only 1 core
    COMPOUND_OPERATOR_UNION,
    COMPOUND_OPERATOR_UNION_ALL,
    COMPOUND_OPERATOR_INTERSECT,
    COMPOUND_OPERATOR_EXCEPT
};

struct SelectCoreData {
    enum CompoundOperatorType   type;
    struct SelectCore           *core;
};

struct SelectStatementNew {
    struct SelectCoreDataPtrList    *cores; // Can have cores linked by compound-operators
    struct OrderByClausePtrList     *order; // Optional
    struct LimitClause              *limit; // Optional
};

struct NewExprCollate {
    struct NewExpr              *expr;
    struct UnterminatedString   collation_name;
};

enum PatternMatchType {
    PATTERN_LIKE,
    PATTERN_GLOB,
    PATTERN_REGEXP,
    PATTERN_MATCH
};

struct NewExprPatternMatch {
    bool                    not;
    enum PatternMatchType   type;
    struct NewExpr          *left;
    struct NewExpr          *right;

    struct NewExpr          *escape; // Only if PATTERN_LIKE
};

struct NewExprBinary {
    enum BinaryOp   op;
    struct NewExpr  *left;
    struct NewExpr  *right;
};

enum NullCompType {
    NULL_COMP_ISNULL,
    NULL_COMP_NOTNULL
};

struct NewExprNullComp {
    enum NullCompType   type;
    struct NewExpr      *expr;
};

struct NewExprIs {
    bool            not;
    bool            distinct;
    struct NewExpr  *left;
    struct NewExpr  *right;
};

struct NewExprBetween {
    bool            not;
    struct NewExpr  *primary;
    struct NewExpr  *left;
    struct NewExpr  *right;
};

struct NewExprIn {
    bool not;
};

enum NewExprType {
    EXPR_LITERAL,
    EXPR_BIND,
    EXPR_NAME,
    EXPR_FUNC,
    EXPR_CAST,
    EXPR_SUBQUERY,
    EXPR_ROW_VALUE,
    EXPR_GROUPING,
    EXPR_EXISTS,
    EXPR_CASE,
    EXPR_RAISE,

    NEW_EXPR_UNARY,
    NEW_EXPR_BINARY,
    NEW_EXPR_COLLATE,
    NEW_EXPR_PATTERN_MATCH,
    NEW_EXPR_NULL_COMP,
    NEW_EXPR_IS,
    NEW_EXPR_BETWEEN,
    NEW_EXPR_IN
};

struct NewExpr {
    enum NewExprType            type;
    struct UnterminatedString   text;

    union {
        // Primaries
        struct NewExprLiteral       *literal;
        struct NewExprBind          *bind;
        struct QualifiedName        *name;
        struct NewExprFunction      *function;
        struct NewExprCast          *cast;
        struct SelectStatementNew   *subquery;
        struct NewExprRowValue      *row_value;
        struct NewExprGrouping      *grouping;
        struct NewExprExists        *exists;
        struct NewExprCase          *expr_case;
        struct NewExprRaise         *raise;

        // 
        struct NewExprUnary         *unary;
        struct NewExprBinary        *binary;
        struct NewExprCollate       *collate;
        struct NewExprPatternMatch  *pattern_match;
        struct NewExprNullComp      *null_comp;
        struct NewExprIs            *is;
        struct NewExprBetween       *between;
        struct NewExprIn            *in;

    };
};

DEFINE_VECTOR(struct NewExpr *, NewExprPtrList, new_expr_ptr_list)
DEFINE_VECTOR(struct CaseClause, CaseClauseList, case_clause_list)
DEFINE_VECTOR(struct OrderByClause *, OrderByClausePtrList, order_by_clause_ptr_list)
DEFINE_VECTOR(struct ResultColumn *, ResultColumnPtrList, result_column_ptr_list)
DEFINE_VECTOR(struct TableExpression *, TableExpressionPtrList, table_expression_ptr_list)
DEFINE_VECTOR(struct JoinData *, JoinDataPtrList, join_data_ptr_list)
DEFINE_VECTOR(struct UnterminatedString, UnterminatedStringList, unterminated_string_list)
DEFINE_VECTOR(struct WindowData *, WindowDataPtrList, window_data_ptr_list)
DEFINE_VECTOR(struct NewExprPtrList *, NewExprPtrListPtrList, new_expr_ptr_list_ptr_list)
DEFINE_VECTOR(struct SelectCoreData *, SelectCoreDataPtrList, select_core_data_ptr_list)
#endif
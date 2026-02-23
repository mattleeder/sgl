#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "sql_utils.h"
#include "comparisons.h"

void print_expr_star_to_stderr(int padding) {
    fprintf(stderr, "%*sStar\n", padding, "");
}

void print_expr_column_to_stderr(struct ExprColumn *column, int padding) {
    assert(column != NULL);

    fprintf(stderr, "%*sColumn\n", padding, "");
    fprintf(stderr, "%*sName: %*s\n", padding + 4, "", column->name.len, column->name.start);
}

void print_expr_function_to_stderr(struct ExprFunction *function, int padding) {
    assert(function != NULL);

    fprintf(stderr, "%*sFunction\n", padding, "");
    fprintf(stderr, "%*sName: %s\n", padding + 4, "", function->name);
    print_expression_list_to_stderr(function->args, padding + 4);
}

void print_expr_binary_to_stderr(struct ExprBinary *binary, int padding) {
    assert(binary != NULL);

    fprintf(stderr, "%*sBinary\n", padding, "");
    fprintf(stderr, "%*sOp: %d\n", padding + 4, "", binary->op);
    fprintf(stderr, "%*sLeft\n", padding + 4, "");
    print_expression_to_stderr(binary->left, padding + 8);
    fprintf(stderr, "%*sRight\n", padding + 4, "");
    print_expression_to_stderr(binary->right, padding + 8);
}

void print_expr_string_to_stderr(struct ExprString *string, int padding) {
    assert(string != NULL);

    fprintf(stderr, "%*sString\n", padding, "");
    fprintf(stderr, "%*s%.*s\n", padding + 4, "", (int)string->len, string->start);
}

void print_expression_to_stderr(struct Expr *expr, int padding) {
    assert(expr != NULL);

    fprintf(stderr, "%*sExpression\n", padding, "");

    switch(expr->type) {
        case EXPR_COLUMN:
            print_expr_column_to_stderr(&expr->column, padding + 4);
            break;

        case EXPR_FUNCTION:
            print_expr_function_to_stderr(&expr->function, padding + 4);
            break;

        case EXPR_STAR:
            print_expr_star_to_stderr(padding + 4);
            break;

        case EXPR_BINARY:
            print_expr_binary_to_stderr(&expr->binary, padding + 4);
            break;

        case EXPR_STRING:
            print_expr_string_to_stderr(&expr->string, padding + 4);
            break;

        default:
            fprintf(stderr, "%.*sPrinting not implemented for expression type %d\n", padding + 4, "", expr->type);
            break;
    }
}

void print_expression_list_to_stderr(struct ExprList *expr_list, int padding) {
    if (expr_list == NULL) {
        fprintf(stderr, "print_expression_list_to_stderr: expr_list is NULL\n");
        exit(1);
    }

    fprintf(stderr, "%*sExpressionList: %lld expressions\n", padding, "", expr_list->count);
    for (int i = 0; i < expr_list->count; i++) {
        print_expression_to_stderr(&expr_list->data[i], padding + 4);
    }
}

void print_from_statement_to_stderr(char* from_table, int padding) {
    if (from_table == NULL) {
        fprintf(stderr, "print_from_statement_to_stderr: from_table is NULL.\n");
        exit(1);
    }

    fprintf(stderr, "%*sFrom: %s\n", padding, "", from_table);
}

void print_select_statement_to_stderr(struct SelectStatement *stmt, int padding) {
    if(stmt == NULL) {
        fprintf(stderr, "print_select_statement_to_stderr: stmt is NULL\n");
        exit(1);
    }

    if (padding < 0) {
        fprintf(stderr, "print_select_statement_to_stderr: negative padding\n");
        exit(1);
    }

    fprintf(stderr, "\n\nPrinting Select Statement\n\n");
    fprintf(stderr, "%*sSelectStatement\n", padding, "");
    
    if (stmt->select_list == NULL) {
        fprintf(stderr, "stmt->select_list is NULL\n");
        exit(1);
    }
    print_expression_list_to_stderr(stmt->select_list, padding + 4);
    print_from_statement_to_stderr(stmt->from_table, padding + 4);    

    if (stmt->where_list != NULL) {
        fprintf(stderr, "%*sWhere\n", padding, "");
        print_expression_list_to_stderr(stmt->where_list, padding + 4);
    }

    fprintf(stderr, "\n\nPrinting Complete\n\n");
}

void print_binary_expr_list_to_stderr(struct BinaryExprList *expr_list, int padding) {
    assert(expr_list != NULL);
    struct ExprBinary binary;
    for (size_t i = 0; i < expr_list->count; i++) {
        binary = expr_list->data[i];
        print_expr_binary_to_stderr(&binary, padding + 4);
    }
}

void get_column_from_expression(struct Expr *expr, struct ColumnToBoolHashMap *columns) {
    switch(expr->type) {
        case EXPR_INTEGER:
        case EXPR_STRING:
        case EXPR_FUNCTION:
            break;

        case EXPR_COLUMN: {
            struct Column column = { .index = 0, .name = {.start = expr->column.name.start, .len = expr->column.name.len } };
            hash_map_column_to_bool_set(columns, column, true);
            break;
        }

        case EXPR_BINARY:
            get_column_from_expression(expr->binary.left, columns);
            get_column_from_expression(expr->binary.right, columns);
            break;

        case EXPR_UNARY:
            get_column_from_expression(expr->unary.right, columns);
            break;

        default:
            fprintf(stderr, "get_column_from_expression: unknown expr->type %d\n", expr->type);
            exit(1);
    }
}

struct ColumnToBoolHashMap *get_columns_from_expression_list(struct ExprList *expr_list) {
    struct ColumnToBoolHashMap *columns = malloc(sizeof(struct ColumnToBoolHashMap));
    if (!columns) {
        fprintf(stderr, "get_columns_from_expression_list: failed to malloc *columns.\n");
        exit(1);
    }

    hash_map_column_to_bool_init(columns, 8, 0.75);

    struct Expr *expr;
    for (int i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        get_column_from_expression(expr, columns);
    }

    return columns;
}

struct IndexComparisonArray *get_index_comparisons(struct ExprList *expr_list) {
    struct IndexComparisonArray *array = malloc(sizeof(struct IndexComparisonArray));
    
    if (!array) {
        fprintf(stderr, "get_column_index_comparisons: failed to malloc *array.\n");
        exit(1);
    }

    init_index_comparison_array(array);

    struct Expr *expr;
    for (int i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        
        if (expr->type != EXPR_BINARY) {
            continue;
        }

        struct Columns *columns = malloc(sizeof(struct Columns));
        if (!columns) {
            fprintf(stderr, "get_column_index_comparisons: failed to malloc columns\n");
            exit(1);
        }

        get_column_from_expression(expr, columns);
        if (columns->count == 0) {
            free(columns);
            continue;
        }

        struct IndexComparison comparison = { .binary = expr->binary, .columns = columns };
        push_index_comparison_array(array, comparison);
    }

    return array;
}
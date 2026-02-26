#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "sql_utils.h"
#include "comparisons.h"
#include "memory.h"

#define HASH_MAP_MIN_CAPACITY (8)

void print_expr_star_to_stderr(int padding) {
    fprintf(stderr, "%*sStar\n", padding, "");
}

void print_expr_column_to_stderr(struct ExprColumn *column, int padding) {
    assert(column != NULL);

    fprintf(stderr, "%*sColumn\n", padding, "");
    fprintf(stderr, "%*sName: %.*s\n", padding + 4, "", (int)column->name.len, column->name.start);
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
    fprintf(stderr, "%*s%.*s\n", padding + 4, "", (int)string->string.len, string->string.start);
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

    fprintf(stderr, "%*sExpressionList: %zu expressions\n", padding, "", expr_list->count);
    for (size_t i = 0; i < expr_list->count; i++) {
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

void get_column_from_expression(struct Expr *expr, struct HashMap *columns) {
    switch(expr->type) {
        case EXPR_INTEGER:
        case EXPR_STRING:
        case EXPR_FUNCTION:
            break;

        case EXPR_COLUMN: {
            struct Column column = { .index = 0, .name = {.start = expr->column.name.start, .len = expr->column.name.len } };
            bool set_value = true;
            hash_map_column_to_bool_set(columns, &column, &set_value);
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

struct HashMap *get_columns_from_expression_list(struct ExprList *expr_list) {
    struct HashMap *columns = hash_map_column_to_bool_new(
        HASH_MAP_MIN_CAPACITY,
        0.75,
        hash_column_ptr,
        equals_column_ptr
    );

    struct Expr *expr;
    for (size_t i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        get_column_from_expression(expr, columns);
    }

    return columns;
}

struct IndexComparisonArray *get_index_comparisons(struct ExprList *expr_list) {
    struct IndexComparisonArray *array = vector_index_comparison_array_new();

    struct Expr *expr;
    for (size_t i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        
        if (expr->type != EXPR_BINARY) {
            continue;
        }

        struct HashMap *column_to_bool_hash_map = hash_map_column_to_bool_new(
            HASH_MAP_MIN_CAPACITY,
            0.75,
            hash_column_ptr,
            equals_column_ptr
        );

        get_column_from_expression(expr, column_to_bool_hash_map);
        if (column_to_bool_hash_map->element_count == 0) {
            hash_map_column_to_bool_free(column_to_bool_hash_map);
            continue;
        }

        size_t number_of_columns;
        struct Column **column_keys = hash_map_column_to_bool_get_keys_alloc(column_to_bool_hash_map, &number_of_columns);

        struct Columns *columns = vector_columns_new();
        for (size_t i = 0; i < number_of_columns; i++) {
            vector_columns_push(columns, *column_keys[i]);
        }
        

        struct IndexComparison comparison = { .binary = expr->binary, .columns = columns };
        vector_index_comparison_array_push(array, comparison);

        hash_map_column_to_bool_free(column_to_bool_hash_map);
    }

    return array;
}
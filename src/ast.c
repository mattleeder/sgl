#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "sql_utils.h"

void print_expr_star_to_stderr(int padding) {
    fprintf(stderr, "%*sStar\n", padding, "");
}

void print_expr_column_to_stderr(struct Expr *expr, int padding) {
    fprintf(stderr, "%*sColumn\n", padding, "");
    fprintf(stderr, "%*sName: %*s\n", padding + 4, "", expr->column.len, expr->column.start);
}

void print_expr_function_to_stderr(struct Expr *expr, int padding) {
    fprintf(stderr, "%*sFunction\n", padding, "");
    fprintf(stderr, "%*sName: %s\n", padding + 4, "", expr->function.name);
    print_expression_list_to_stderr(expr->function.args, padding + 4);
}

void print_expr_binary_to_stderr(struct Expr *expr, int padding) {
    fprintf(stderr, "%*sBinary\n", padding, "");
    fprintf(stderr, "%*sOp: %d\n", padding + 4, "", expr->binary.op);
    fprintf(stderr, "%*sLeft\n", padding + 4, "");
    print_expression_to_stderr(expr->binary.left, padding + 8);
    fprintf(stderr, "%*sRight\n", padding + 4, "");
    print_expression_to_stderr(expr->binary.right, padding + 8);
}

void print_expr_string_to_stderr(struct Expr *expr, int padding) {
    fprintf(stderr, "%*sString\n", padding, "");
    fprintf(stderr, "%*s%.*s\n", padding + 4, "", (int)expr->string.len, expr->string.start);
}

void print_expression_to_stderr(struct Expr *expr, int padding) {
    fprintf(stderr, "%*sExpression\n", padding, "");

    switch(expr->type) {
        case EXPR_COLUMN:
            print_expr_column_to_stderr(expr, padding + 4);
            break;

        case EXPR_FUNCTION:
            print_expr_function_to_stderr(expr, padding + 4);
            break;

        case EXPR_STAR:
            print_expr_star_to_stderr(padding + 4);
            break;

        case EXPR_BINARY:
            print_expr_binary_to_stderr(expr, padding + 4);
            break;

        case EXPR_STRING:
            print_expr_string_to_stderr(expr, padding + 4);
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

void get_column_from_expression(struct Expr *expr, struct Columns *columns) {
    switch(expr->type) {
        case EXPR_INTEGER:
        case EXPR_STRING:
        case EXPR_FUNCTION:
            break;

        case EXPR_COLUMN:
            struct Column column = { .index = 0, .name_start = expr->column.start, .name_length = expr->column.len };
            push_columns(columns, column);
            break;

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

struct Columns *get_columns_from_expression_list(struct ExprList *expr_list) {
    struct Columns *columns = malloc(sizeof(struct Columns));
    init_columns(columns);

    struct Expr *expr;
    for (int i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        get_column_from_expression(expr, columns);
    }

    return columns;
}
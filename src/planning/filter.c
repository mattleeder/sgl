#include <stdio.h>
#include <stdbool.h>

#include "../ast.h"
#include "../sql_utils.h"
#include "../data_parsing/row_parsing.h"
#include "plan.h"

static struct Filter {
    struct Plan     base;
    struct Plan     *child;
    struct ExprList *predicates;
    struct Columns  *columns;
};

static bool value_is_equal(struct Value *left, struct Value *right) {
    // Types should have already been checked for equality

    
    switch (left->type) {

        case VALUE_INT:
            return left->int_value.value == right->int_value.value;

        case VALUE_TEXT:
            // fprintf(stderr, "%.*s == %.*s\n", left->text_value.len, left->text_value.data, right->text_value.len, right->text_value.data);
            if (left->text_value.len != right->text_value.len) {
                return false;
            }
            return (memcmp(left->text_value.data, right->text_value.data, left->text_value.len) == 0);

        case VALUE_NULL:
            // NULL == NULL
            return true;
        
        case VALUE_FLOAT:
            fprintf(stderr, "Float currently unsupported.\n");
            exit(1);

        default:
            fprintf(stderr, "Unsupported type %d.\n", left->type);
            exit(1);
    }
}

static bool value_is_less(struct Value *left, struct Value *right) {
    // Types should have already been checked for equality
    
    switch (left->type) {

        case VALUE_INT:
            return left->int_value.value < right->int_value.value;

        case VALUE_TEXT: {
            // @TODO: edge case where prefixes are same but lengths are different
            size_t length = (left->text_value.len <= right->text_value.len) ? left->text_value.len : right->text_value.len;
            return (strncmp(left->text_value.data, right->text_value.data, length) < 0);
        }

        case VALUE_NULL:
            // NULL == NULL
            return false;
        
        case VALUE_FLOAT:
            fprintf(stderr, "Float currently unsupported.\n");
            exit(1);

        default:
            fprintf(stderr, "Unsupported type %d.\n", left->type);
            exit(1);
    }
}

static bool value_is_greater(struct Value *left, struct Value *right) {
    // Types should have already been checked for equality
    
    switch (left->type) {

        case VALUE_INT:
            return left->int_value.value > right->int_value.value;

        case VALUE_TEXT: {
            // @TODO: edge case where prefixes are same but lengths are different
            size_t length = left->text_value.len <= right->text_value.len ? left->text_value.len : right->text_value.len;
            return (strncmp(left->text_value.data, right->text_value.data, length) > 0);
        }

        case VALUE_NULL:
            // NULL == NULL
            return false;
        
        case VALUE_FLOAT:
            fprintf(stderr, "Float currently unsupported.\n");
            exit(1);

        default:
            fprintf(stderr, "Unsupported type %d.\n", left->type);
            exit(1);
    }
}

static struct Value expr_to_value(struct Expr *expr, struct Row *row, struct Columns *columns) {
    struct Value value;

    switch (expr->type) {

        case EXPR_INTEGER:
            value.type              = VALUE_INT;
            value.int_value.value   = expr->integer.value;
            break;
            
        case EXPR_STRING:
            value.type              = VALUE_TEXT;
            value.text_value.data   = expr->string.start;
            value.text_value.len    = expr->string.len;
            break;

        case EXPR_COLUMN: {
            bool found = false;
            for (int i = 0; i < columns->count; i++) {
                struct Column column = columns->data[i];
                if (expr->column.len != column.name_length || strncmp(expr->column.start, column.name_start, column.name_length) != 0) {
                    continue;
                }

                found = true;
                uint32_t index = column.index;
                if (index >= row->column_count) {
                    fprintf(stderr, "expr_to_value: Index %d out of bounds %d.\n", index, row->column_count);
                    print_row_to_stderr(row);
                    exit(1);
                }
                value = row->values[index];
                break;
            }

            if (!found) {
                fprintf(stderr, "expr_to_value: Could not find column %*s.\n", expr->column.len, expr->column.start);
                exit(1);
            }
            break;

        default:
            fprintf(stderr, "Unsupported Expr to Value conversion %d\n.", expr->type);
            exit(1);
        }
    }

    return value;
}

static bool evaluate_predicate(struct Expr *predicate, struct Row *row, struct Columns *columns) {

    if (predicate->type != EXPR_BINARY) {
        fprintf(stderr, "Currently only binary predicates are supported.\n");
        exit(1);
    }

    struct Value left_value     = expr_to_value(predicate->binary.left, row, columns);
    struct Value right_value    = expr_to_value(predicate->binary.right, row, columns);

    if (left_value.type != right_value.type) {
        // fprintf(stderr, "evaluate_predicate: mismatched types %d, %d.\n", left_value.type, right_value.type);
        return false;
    }

    bool result = false;
    switch (predicate->binary.op) {

        case BIN_EQUAL:
            result = value_is_equal(&left_value, &right_value);
            break;

        case BIN_LESS:
            result = value_is_less(&left_value, &right_value);
            break;

        case BIN_GREATER:
            result = value_is_greater(&left_value, &right_value);
            break;

        default:
            fprintf(stderr, "Op unsupported %d.\n", predicate->binary.op);
            exit(1);

    }

    return result;
}


static struct Plan *make_filter(struct Plan *plan, struct ExprList *predicates, struct Columns *columns) {
    struct Filter *filter = malloc(sizeof(struct Filter));
    if (!filter) {
        fprintf(stderr, "make_filter: *filter malloc failed\n");
        exit(1);
    }

    memset(filter, 0, sizeof *filter);
    filter->base.type           = PLAN_FILTER;
    filter->predicates          = predicates;
    filter->columns             = columns;
    filter->child               = plan;
    return &filter->base;
}


static bool filter_next(struct Pager *pager, struct Filter *filter, struct Row *row) {
    // Filter rows based on predicate

    while (plan_next(pager, filter->child, row)) {
        bool predicate_success = true;

        for (int i = 0; i < filter->predicates->count; i++) {

            struct Expr *predicate = filter->predicates->list[i];
            if (!evaluate_predicate(predicate, row, filter->columns)) {
                predicate_success = false;
                break;
            }
        }

        if (predicate_success) {
            return true;
        }
    }

    return false;
}
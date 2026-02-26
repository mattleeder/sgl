#include <stdio.h>
#include <stdbool.h>

#include "filter.h"
#include "../ast.h"
#include "../sql_utils.h"
#include "../data_parsing/row_parsing.h"
#include "plan.h"



static bool value_is_equal(struct Value *left, struct Value *right) {
    // Types should have already been checked for equality

    
    switch (left->type) {

        case VALUE_INT:
            return left->int_value.value == right->int_value.value;

        case VALUE_TEXT:
            // fprintf(stderr, "%.*s == %.*s\n", left->text_value.len, left->text_value.data, right->text_value.len, right->text_value.data);
            return unterminated_string_equals(&left->text_value.text, &right->text_value.text);

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
            return unterminated_string_less_than(&left->text_value.text, &right->text_value.text);
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
            return unterminated_string_greater_than(&left->text_value.text, &right->text_value.text);
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

static struct Value expr_to_value(struct Expr *expr, struct Row *row) {
    struct Value value;

    switch (expr->type) {

        case EXPR_INTEGER:
            value.type              = VALUE_INT;
            value.int_value.value   = expr->integer.value;
            break;
            
        case EXPR_STRING:
            value.type              = VALUE_TEXT;
            value.text_value.text   = expr->string.string;
            break;

        case EXPR_COLUMN: {
            value = row->values[expr->column.idx];
            break;

        default:
            fprintf(stderr, "Unsupported Expr to Value conversion %d\n.", expr->type);
            exit(1);
        }
    }

    return value;
}

static bool evaluate_predicate(struct Expr *predicate, struct Row *row) {

    if (predicate->type != EXPR_BINARY) {
        fprintf(stderr, "Currently only binary predicates are supported.\n");
        exit(1);
    }

    struct Value left_value     = expr_to_value(predicate->binary.left, row);
    struct Value right_value    = expr_to_value(predicate->binary.right, row);

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
            fprintf(stderr, "evaluate_predicate: Op unsupported %d.\n", predicate->binary.op);
            exit(1);

    }

    return result;
}


struct Plan *make_filter(struct Plan *plan, struct ExprList *predicates) {
    struct Filter *filter = malloc(sizeof(struct Filter));
    if (!filter) {
        fprintf(stderr, "make_filter: *filter malloc failed\n");
        exit(1);
    }

    memset(filter, 0, sizeof *filter);
    filter->base.type           = PLAN_FILTER;
    filter->predicates          = predicates;
    filter->child               = plan;
    return &filter->base;
}


bool filter_next(struct Pager *pager, struct Filter *filter, struct Row *row) {
    // Filter rows based on predicate
    // @TODO: doesnt need to filter rows already filtered by index
    while (plan_next(pager, filter->child, row)) {
        fprintf(stderr, "Filter\n");
        bool predicate_success = true;

        for (size_t i = 0; i < filter->predicates->count; i++) {

            struct Expr *predicate = &filter->predicates->data[i];
            if (!evaluate_predicate(predicate, row)) {
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
#include "ast.h"
#include "sql_utils.h"
#include "data_parsing/row_parsing.h"

struct Value get_predicate_value(struct ExprBinary *predicate) {
    if (predicate->left->type == EXPR_COLUMN && predicate->right->type == EXPR_COLUMN) {
        fprintf(stderr, "get_predicate_value: Currently cannot handle multiple index.\n");
        exit(1);
    }

    struct Expr *expr_to_convert = predicate->left->type != EXPR_COLUMN ? predicate->left : predicate->right;
    struct Value value;

    switch (expr_to_convert->type) {

        case EXPR_INTEGER:
            value.type              = VALUE_INT;
            value.int_value.value   = expr_to_convert->integer.value;
            break;

        case EXPR_STRING:
            value.type              = VALUE_TEXT;
            value.text_value.text   = expr_to_convert->string.string;
            break;

        default:
            fprintf(stderr, "get_predicate_value: Unsupported conversion: %d\n", expr_to_convert->type);
            exit(1);
    }

    return value;
}

int compare_values(struct Value *left, struct Value *right) {
    // Types should have already been checked for equality

    switch (left->type) {

        case VALUE_INT:
            if (left->int_value.value == right->int_value.value) return 0;
            if (left->int_value.value < right->int_value.value) return -1;
            return 1;

        case VALUE_TEXT: {
            size_t len_value = left->text_value.text.len < right->text_value.text.len ? left->text_value.text.len : right->text_value.text.len;
            int result = memcmp(left->text_value.text.start, right->text_value.text.start, len_value);
            if (result != 0) return result;
            if (result == 0 && left->text_value.text.len == right->text_value.text.len) return 0;
            if (left->text_value.text.len < right->text_value.text.len) return -1;
            return 1;
        }

        case VALUE_NULL:
            // NULL == NULL
            return 0;
        
        case VALUE_FLOAT:
            fprintf(stderr, "Float currently unsupported.\n");
            exit(1);

        default:
            fprintf(stderr, "Unsupported type %d.\n", left->type);
            exit(1);
    }
}

bool compare_index_predicate(struct ExprBinary *predicate, struct Value *column_value, struct Value *predicate_value) {
    // For use with binary search
    // Return true if hi should come down
    // Return false if lo should go up

    if (column_value->type != predicate_value->type) {
        return false;
    }

    bool result = false;
    int cmp_result = compare_values(column_value, predicate_value);
    switch (predicate->op) {

        case BIN_EQUAL:
            if (cmp_result >= 0) result = true;
            break;

        case BIN_LESS:
            result = true;
            break;

        case BIN_GREATER:
            if (cmp_result <= 0) result = true;
            break;

        default:
            fprintf(stderr, "compare_index_predicate: Op unsupported %d.\n", predicate->op);
            exit(1);

    }

    if (result) {
        // fprintf(stderr, "Search to left Comparison result: %d, predicate result: %d\n", cmp_result, result);
    } else {
        // fprintf(stderr, "Search to right Comparison result: %d, predicate result: %d\n", cmp_result, result);
    }

    return result;
}

// I need a function that will take index constraints
// and an index row and perform the comparisons

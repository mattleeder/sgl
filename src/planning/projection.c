#include <stdio.h>
#include <stdbool.h>

#include "../ast.h"
#include "../sql_utils.h"
#include "plan.h"
#include "../data_parsing/row_parsing.h"

struct Projection {
    struct Plan     base;
    struct Plan     *child;
    struct ExprList *select_list;
    struct Columns  *columns;
};

static struct Plan *make_projection(struct Plan *plan, struct ExprList *select_list, struct Columns *columns) {
    struct Projection *projection = malloc(sizeof(struct Projection));
    if (!projection) {
        fprintf(stderr, "make_projection: *projection malloc failed\n");
        exit(1);
    }

    memset(projection, 0, sizeof *projection);
    projection->base.type    = PLAN_PROJECTION;
    projection->child        = plan;
    projection->select_list  = select_list;
    projection->columns      = columns;

    for (int i = 0; i < columns->count; i++) {
        struct Column column = columns->data[i];
        fprintf(stderr, "make_projection: Column %d: %.*s\n", i, (int)column.name_length, column.name_start);
    }

    return &projection->base;
}

static bool projection_next(struct Pager *pager, struct Projection *projection, struct Row *row) {
    // Only write selected columns into output
    if (!plan_next(pager, projection->child, row)) {
        return false;
    }

    // print_row_to_stderr(row);
    struct Row original_row_order;
    original_row_order.column_count = row->column_count;
    original_row_order.values = malloc(row->column_count * sizeof(struct Value));
    if (!original_row_order.values) {
        fprintf(stderr, "projection_next: *original_row_order.values malloc failed\n");
        exit(1);
    }
    memcpy(original_row_order.values, row->values, row->column_count * sizeof(struct Value));


    for (int i = 0; i < projection->select_list->count; i++) {
        struct Expr *current_expr = projection->select_list->list[i];

        switch (current_expr->type) {

            case EXPR_COLUMN:
                // @TODO: should only be calculating this once
                // Iterate over all columns, if there is a match move that value to column index
                // and then increment
                for (int j = 0; j < projection->columns->count; j++) {
                    struct Column column = projection->columns->data[j];
                    char *column_name   = column.name_start;
                    size_t name_length  = column.name_length;
                    uint32_t index      = column.index;
                    if (name_length == current_expr->column.len && strncmp(column_name, current_expr->column.start, name_length) == 0) {
                        if (i != index) {
                            row->values[i] = original_row_order.values[index];
                        }
                        break;
                    }
                }
                break;

            case EXPR_FUNCTION:
                // @TODO: find function
                fprintf(stderr, "Printing function\n");
                row->column_count = 2; // Id and result
                break;

            default:
                fprintf(stderr, "Unsupported ExprType for projection_next: %d\n", current_expr->type);
                exit(1);
        }
    }

    free(original_row_order.values);
    row->column_count = projection->select_list->count;
    return true;
}
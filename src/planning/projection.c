#include <stdio.h>
#include <stdbool.h>

#include "../ast.h"
#include "../sql_utils.h"
#include "plan.h"
#include "../data_parsing/row_parsing.h"

struct Projection {
    struct Plan     base;
    struct Plan     *child;
    struct SizeTVec *column_indexes;
    bool            first_col_is_rowid;
};

static struct Plan *make_projection(struct Plan *plan, struct SizeTVec *indexes, bool first_col_is_rowid) {
    struct Projection *projection = malloc(sizeof(struct Projection));
    if (!projection) {
        fprintf(stderr, "make_projection: *projection malloc failed\n");
        exit(1);
    }

    projection->base.type           = PLAN_PROJECTION;
    projection->child               = plan;
    projection->column_indexes      = indexes;
    projection->first_col_is_rowid  = first_col_is_rowid;

    fprintf(stderr, "Projection: first_col_is_rowid: %d\n", first_col_is_rowid);

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

    if (projection->first_col_is_rowid) {
        row->values[0] = (struct Value){ .type = VALUE_INT, .int_value = { .value = row->rowid } };
    }

    for (size_t i = projection->first_col_is_rowid ? 1 : 0; i < projection->column_indexes->count; i++) {
        size_t idx = projection->column_indexes->data[i];
        row->values[i] = original_row_order.values[idx];
    }

    free(original_row_order.values);
    row->column_count = projection->column_indexes->count;
    return true;
}
#include <stdio.h>
#include <stdbool.h>

#include "../ast.h"
#include "plan.h"
#include "../data_parsing/row_parsing.h"

struct Aggregate {
    struct Plan     base;
    struct Plan     *child;
    bool            done;
    struct ExprList *aggregates;
};

static struct Plan *make_aggregate(struct Plan *plan, struct ExprList *aggregates) {
    struct Aggregate *aggregate = malloc(sizeof(struct Aggregate));
    if (!aggregate) {
        fprintf(stderr, "make_aggregate: *aggregate malloc failed\n");
        exit(1);
    }

    memset(aggregate, 0, sizeof *aggregate);
    aggregate->base.type            = PLAN_AGGREGATE;
    aggregate->child                = plan;
    aggregate->done                 = false;
    aggregate->aggregates           = aggregates;
    return &aggregate->base;
}

static bool aggregate_next(struct Pager *pager, struct Aggregate *aggregate, struct Row *row) {
    // Keep consuming rows until aggregation complete
    if (aggregate->done) {
        return false;
    }

    struct Value *results = malloc(sizeof(struct Value) * aggregate->aggregates->count);
    if (!results) {
        fprintf(stderr, "aggregate_next: *results malloc failed\n");
        exit(1);
    }

    for (int i = 0; i < aggregate->aggregates->count; i++) {
        results[i].type                 = VALUE_NULL;
        results[i].null_value.null_ptr  = NULL;
    }
    
    while (plan_next(pager, aggregate->child, row)) {

        for (int i = 0; i < aggregate->aggregates->count; i++) {
            struct Expr *current_aggregate = &aggregate->aggregates->data[i];
            if (current_aggregate->type != EXPR_FUNCTION) {
                fprintf(stderr, "Aggregate should be function.\n");
                exit(1);
            }

            struct Value *value = &results[i];
            switch (current_aggregate->function.agg_type) {

                case AGG_COUNT:
                    value->type = VALUE_INT;
                    value->int_value.value++;
                    break;

                default:
                    fprintf(stderr, "Do not recognise aggregate %d.\n", current_aggregate->function.agg_type);
                    exit(1);
            }
        }

    }

    aggregate->done = true;

    free(row->values);
    row->column_count   = aggregate->aggregates->count;
    row->values         = results;

    // @TODO: Aggregate results need to be written somewhere

    return true;
}
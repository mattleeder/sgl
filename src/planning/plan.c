#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "../memory.h"
#include "plan.h"
#include "../sql_utils.h"
#include "../data_parsing/row_parsing.h"
#include "../ast.h"

#include "projection.c"
#include "aggregate.c"
#include "filter.c"
#include "table_scan.c"

static bool is_aggregate_function(char *function_name) {
    return strcmp(function_name, "count") == 0;
}

static bool expr_contains_aggregate(struct Expr *expr) {
    if (!expr) {
        return false;
    }

    switch (expr->type) {
        
        case EXPR_FUNCTION:
            if (is_aggregate_function(expr->function.name)) {
                return true;
            }

            if (expr_list_contains_aggregate(expr->function.args)) {
                return true;
            }

            return false;

        default:
            return false;
    }
}

static bool expr_list_contains_aggregate(struct ExprList *expr_list) {
    for (int i = 0; i < expr_list->count; i++) {
        if (expr_contains_aggregate(&expr_list->data[i])) {
            return true;
        }
    }

    return false;
}

static void collect_aggregates(struct ExprList *expr_list, struct Expr *expr) {
    if (!expr) {
        return;
    }

    if (expr->type == EXPR_FUNCTION && is_aggregate_function(expr->function.name)) {
        push_expr_list(expr_list, *expr);
    }
}

struct Plan *build_plan(struct Pager *pager, struct SelectStatement *stmt) {
    struct TableScan *plan = make_table_scan(pager, stmt);

    fprintf(stderr, "build_plan: columns:\n");

    fprintf(stderr, "build_plan: make filter:\n");
    if (stmt->where_list != NULL) {
        plan = make_filter(plan, stmt->where_list, columns);
    }
    fprintf(stderr, "build_plan: filter made:\n");

    fprintf(stderr, "build_plan: collect aggregates:\n");
    struct ExprList *expr_list = new_expr_list();
    for (int i = 0; i < stmt->select_list->count; i++) {
        collect_aggregates(expr_list, &stmt->select_list->data[i]);
    }
    if (expr_list->count > 0) {
        fprintf(stderr, "Plan contains aggregates.\n");
        plan = make_aggregate(plan, expr_list);
    }
    fprintf(stderr, "build_plan: aggregates collected:\n");

    fprintf(stderr, "build_plan: make projection:\n");
    plan = make_projection(plan, pager, stmt);
    fprintf(stderr, "build_plan: projection made:\n");
    return plan;
}

static bool plan_next(struct Pager *pager, struct Plan *plan, struct Row *row) {
    if (plan == NULL) {
        fprintf(stderr, "plan_next: NULL plan\n");
        exit(1);
    }

    switch(plan->type) {

        case PLAN_TABLE_SCAN:
            return table_scan_next(pager, plan, row);

        case PLAN_AGGREGATE:
            return aggregate_next(pager, (struct Aggregate *)plan, row);

        case PLAN_FILTER:
            return filter_next(pager, (struct Filter *)plan, row);

        case PLAN_PROJECTION:
            return projection_next(pager, plan, row);

        default:
            return false;
    }
}

void plan_execute(struct Pager *pager, struct Plan *plan) {
    fprintf(stderr, "Execute Plan\n");
    struct Row row;
    while (plan_next(pager, plan, &row)) {
        print_row(&row);
        free(row.values);
        row.values = NULL;
        row.column_count = 0;
    }
}
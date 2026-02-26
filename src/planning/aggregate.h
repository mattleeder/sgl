#ifndef sql_aggregate
#define sql_aggregate

#include "plan.h"

struct Aggregate {
    struct Plan     base;
    struct Plan     *child;
    bool            done;
    struct ExprList *aggregates;
};

struct Plan *make_aggregate(struct Plan *plan, struct ExprList *aggregates);
bool aggregate_next(struct Pager *pager, struct Aggregate *aggregate, struct Row *row);

#endif
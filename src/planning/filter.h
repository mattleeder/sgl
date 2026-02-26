#ifndef sql_filter
#define sql_filter

#include "plan.h"

struct Filter {
    struct Plan     base;
    struct Plan     *child;
    struct ExprList *predicates;
};

struct Plan *make_filter(struct Plan *plan, struct ExprList *predicates);
bool filter_next(struct Pager *pager, struct Filter *filter, struct Row *row);

#endif
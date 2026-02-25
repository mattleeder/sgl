#ifndef sql_plan
#define sql_plan

#include <stdint.h>

#include "../ast.h"
#include "../data_parsing/row_parsing.h"

DEFINE_VECTOR(size_t, SizeTVec, size_t)

enum PlanType {
    PLAN_TABLE_SCAN,
    PLAN_FILTER,
    PLAN_PROJECTION,
    PLAN_AGGREGATE
};



struct Plan {
    enum PlanType type;
};

struct Index {
    struct UnterminatedString   column_name;
    struct ExprBinary           *predicate;
    uint32_t                    root_page;
    // Column index applies to
    // Condition
};

static bool expr_contains_aggregate(struct Expr *expr);
static bool expr_list_contains_aggregate(struct ExprList *expr_list);
static bool plan_next(struct Pager *pager, struct Plan *plan, struct Row *row);

struct Plan *build_plan(struct Pager *pager, struct SelectStatement *stmt);
void plan_execute(struct Pager *pager, struct Plan *plan);

#endif sql_plan
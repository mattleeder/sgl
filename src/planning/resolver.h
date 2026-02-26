#ifndef sql_resolver
#define sql_resolver

#include <stdint.h>

#include "../memory.h"
#include "../sql_utils.h"
#include "plan.h"

struct Resolver {
    struct HashMap *full_row_col_to_idx;
    struct HashMap *post_agg_row_col_to_idx;
    bool            first_col_is_rowid;
    bool            query_has_aggregates;
};

void resolve_column_names(struct Resolver *resolver, struct ExprList *expr_list, enum PlanType type);
struct Resolver *new_resolver(bool query_has_aggregates);
struct Resolver *resolver_init(struct Resolver *resolver, struct Pager *pager, struct SelectStatement *stmt);
struct SizeTVec *get_projection_indexes(struct Resolver *resolver, struct SelectStatement *stmt);

#endif
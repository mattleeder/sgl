#ifndef sql_projection
#define sql_projection

#include "plan.h"

struct Projection {
    struct Plan     base;
    struct Plan     *child;
    struct SizeTVec *column_indexes;
    bool            first_col_is_rowid;
};

struct Plan *make_projection(struct Plan *plan, struct SizeTVec *indexes, bool first_col_is_rowid);
bool projection_next(struct Pager *pager, struct Projection *projection, struct Row *row);

#endif
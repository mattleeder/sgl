#ifndef sql_table_scan
#define sql_table_scan

#include "plan.h"

struct TableScan {
    struct Plan         base;
    size_t              row_cursor;
    uint32_t            root_page;
    bool                first_col_is_row_id;
    char                *table_name;
    struct Columns      *columns;
    struct TreeWalker   *walker;
    struct IndexData    *index;
};

bool table_scan_next(struct TableScan *table_scan, struct Row *row);
struct Plan *make_table_scan(struct Pager *pager, struct SelectStatement *stmt);

#endif
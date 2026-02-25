#ifndef sql_tree_walker
#define sql_tree_walker

#include <stdbool.h>

#include "sql_utils.h"
#include "planning/plan.h"

enum WalkerType {
    WALKER_FULL_SCAN,
    WALKER_INDEX_SCAN
};

struct SubWalker;
struct SubWalkerList;

struct SubWalker {
    uint32_t            page;
    struct Pager        *pager;
    struct PageHeader   *page_header;
    struct Cell         *cell;
    uint16_t            *cell_pointer_array;
    uint16_t            current_index;
    void (*step)(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *rowid_valid);
    struct IndexData    *index;
};

DEFINE_VECTOR(struct SubWalker*, SubWalkerList, sub_walker_list)

struct TreeWalker {
    enum WalkerType         type;
    struct Pager            *pager;
    uint32_t                root_page;
    struct SubWalkerList    *table_list;
    struct SubWalkerList    *index_list;
    struct IndexData        *index;
    uint64_t                current_rowid;
};

struct TreeWalker *new_tree_walker(struct Pager *pager, uint32_t root_page, struct IndexData *index);
void begin_walk(struct SubWalker *walker);
bool produce_row(struct TreeWalker *walker, struct Row *row);

#endif
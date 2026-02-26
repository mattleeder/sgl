#ifndef sql_cell_parsing
#define sql_cell_parsing

#include <stdint.h>

#include "../pager.h"
#include "page_parsing.h"

//@TODO: should we just use page type?
enum CellType {
    TABLE_LEAF_CELL,
    TABLE_INTERIOR_CELL,
    INDEX_LEAF_CELL,
    INDEX_INTERIOR_CELL
};

struct PayloadInfo {
    uint64_t    payload_size;
    uint64_t    local_bytes;
    uint32_t    payload_offset;
    uint32_t    overflow_page;
};

struct TableLeafCell {
    uint64_t            row_id;
    struct PayloadInfo  payload_info;
};

struct TableInteriorCell {
    uint32_t    left_child_pointer;
    uint64_t    integer_key;
};

struct IndexLeafCell {
    struct PayloadInfo  payload_info;
};

struct IndexInteriorCell {
    uint32_t            left_child_pointer;
    struct PayloadInfo  payload_info;
};

struct Cell {
    enum CellType type;
    uint32_t cell_offset;
    uint32_t page_number;
    union {
        struct TableLeafCell        table_leaf_cell;
        struct TableInteriorCell    table_interior_cell;
        struct IndexLeafCell        index_leaf_cell;
        struct IndexInteriorCell    index_interior_cell;
    } data;
};

void read_cell(
    struct Pager *pager, 
    struct PageHeader *page_header, 
    struct Cell *cell, 
    uint16_t cell_offset);

#endif
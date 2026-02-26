#ifndef sql_page_parsing
#define sql_page_parsing

#include <stdint.h>

#include "../pager.h"

#define DATABASE_HEADER_SIZE (100)

#define PAGE_TYPE_OFFSET                    0
#define PAGE_FREEBLOCK_OFFSET               1
#define PAGE_NUMBER_OF_CELLS_OFFSET         3
#define PAGE_CELL_CONTENT_OFFSET            5
#define PAGE_FRAGMENTED_FREE_BYTES_OFFSET   7
#define PAGE_RIGHT_MOST_POINTER_OFFSET      8 // Only for interior pages

enum PageType {
    PAGE_LEAF_TABLE,
    PAGE_INTERIOR_TABLE,
    PAGE_LEAF_INDEX,
    PAGE_INTERIOR_INDEX,
    PAGE_INVALID
};

struct PageHeader {
    enum PageType       page_type;
    uint8_t             header_size;
    uint8_t             fragmented_free_bytes;
    uint32_t            page_number;
    uint16_t            freeblock;
    uint16_t            number_of_cells;
    uint32_t            cell_content;
    uint32_t            right_most_pointer;
    uint64_t            start_of_cell_pointer_array;
};

void read_page_header(struct Pager *pager, struct PageHeader *page_header, uint32_t page_number);
uint16_t *read_cell_pointer_array(struct Pager *pager, struct PageHeader *page_header);
uint16_t *read_page_header_and_cell_pointer_array(struct Pager *pager, struct PageHeader *page_header, uint32_t page_number);

#endif
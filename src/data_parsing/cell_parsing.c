#include <stdint.h>
#include <stdlib.h>

#include "byte_reader.h"
#include "cell_parsing.h"
#include "../pager.h"
#include "page_parsing.h"

static uint64_t get_table_leaf_cell_payload_local_bytes(struct Pager *pager, struct Cell *cell) {
    // Let X be U-35. If the payload size P is less than or equal to X then the entire 
    // payload is stored on the b-tree leaf page. Let M be ((U-12)*32/255)-23 and let 
    // K be M+((P-M)%(U-4)). If P is greater than X then the number of bytes stored on 
    // the table b-tree leaf page is K if K is less or equal to X or M otherwise. 
    // The number of bytes stored on the leaf page is never less than M. 

    uint64_t usable_size  = pager->page_size - pager->database_header->reserved_space;
    uint64_t payload_size = cell->data.table_leaf_cell.payload_info.payload_size;

    uint64_t max_local = usable_size - 35;
    if (payload_size <= max_local) {
        return payload_size;
    }

    uint64_t min_local = ((usable_size - 12) * 32 / 255) - 23;
    uint64_t k = min_local + ((payload_size - min_local) % (usable_size - 4));

    if (k > max_local) {
        k = min_local;
    }
    return k;
}

static uint64_t get_index_cell_payload_local_bytes(struct Pager *pager, struct Cell *cell) {
    // Let X be ((U-12)*64/255)-23. If the payload size P is less than or equal to X 
    // then the entire payload is stored on the b-tree page. Let M be ((U-12)*32/255)-23 
    // and let K be M+((P-M)%(U-4)). If P is greater than X then the number of bytes stored 
    // on the index b-tree page is K if K is less than or equal to X or M otherwise. 
    // The number of bytes stored on the index page is never less than M. 
    if (cell->type != INDEX_LEAF_CELL && cell->type != INDEX_INTERIOR_CELL) {
        fprintf(stderr, "Expected index cell.");
        exit(1);
    }

    uint64_t usable_size  = pager->page_size - pager->database_header->reserved_space;
    uint64_t payload_size = cell->type == INDEX_LEAF_CELL ? cell->data.index_leaf_cell.payload_info.payload_size : cell->data.index_interior_cell.payload_info.payload_size;

    uint64_t max_local = ((usable_size - 12) * 64 / 255) - 23;
    if (payload_size <= max_local) {
        return payload_size;
    }

    uint64_t min_local = ((usable_size - 12) * 32 / 255) - 23;
    uint64_t k = min_local + ((payload_size - min_local) % (usable_size - 4));

    if (k > max_local) {
        k = min_local;
    }
    return k;
}

static void read_table_leaf_cell(struct Pager *pager, const uint8_t *data, struct Cell *cell) {
    // Should only be called from read_cell


    // A varint which is the total number of bytes of payload, including any overflow
    // A varint which is the integer key, a.k.a. "rowid"
    // The initial portion of the payload that does not spill to overflow pages.
    // A 4-byte big-endian integer page number for the first page of the overflow page list - omitted if all payload fits on the b-tree page. 

    uint64_t bytes_read = 0;

    cell->data.table_leaf_cell.payload_info.payload_size        = read_varint(data, &bytes_read);
    cell->data.table_leaf_cell.row_id                           = read_varint(data, &bytes_read);
    cell->data.table_leaf_cell.payload_info.payload_offset      = cell->cell_offset + bytes_read;
    cell->data.table_leaf_cell.payload_info.local_bytes         = get_table_leaf_cell_payload_local_bytes(pager, cell);


    if (cell->data.table_leaf_cell.payload_info.local_bytes < cell->data.table_leaf_cell.payload_info.payload_size) {
        cell->data.table_leaf_cell.payload_info.overflow_page = read_u32_big_endian(data, bytes_read + cell->data.table_leaf_cell.payload_info.local_bytes);
        fprintf(stderr, "Local bytes: %lld, Overflow page: %d\n", cell->data.table_leaf_cell.payload_info.local_bytes, cell->data.table_leaf_cell.payload_info.overflow_page);
    } else {
        cell->data.table_leaf_cell.payload_info.overflow_page = 0;
    }
}

static void read_table_interior_cell(struct Pager *pager, const uint8_t *data, struct Cell *cell) {
    // Should only be called from read_cell


    // A 4-byte big-endian page number which is the left child pointer.
    // A varint which is the integer key 

    uint64_t bytes_read = 0;

    cell->data.table_interior_cell.left_child_pointer   = read_u32_big_endian(data, 0);
    cell->data.table_interior_cell.integer_key          = read_varint(data + 4, &bytes_read);
}

static void read_index_leaf_cell(struct Pager *pager, const uint8_t *data, struct Cell *cell) {
    // Should only be called from read_cell


    // A varint which is the total number of bytes of key payload, including any overflow
    // The initial portion of the payload that does not spill to overflow pages.
    // A 4-byte big-endian integer page number for the first page of the overflow page list - omitted if all payload fits on the b-tree page. 

    uint64_t bytes_read = 0;

    cell->data.index_leaf_cell.payload_info.payload_size        = read_varint(data, &bytes_read);
    cell->data.index_leaf_cell.payload_info.payload_offset      = cell->cell_offset + bytes_read;
    cell->data.index_leaf_cell.payload_info.local_bytes         = get_index_cell_payload_local_bytes(pager, cell);


    if (cell->data.index_leaf_cell.payload_info.local_bytes < cell->data.index_leaf_cell.payload_info.payload_size) {
        cell->data.index_leaf_cell.payload_info.overflow_page = read_u32_big_endian(data, bytes_read + cell->data.index_leaf_cell.payload_info.local_bytes);
        fprintf(stderr, "Local bytes: %lld, Overflow page: %d\n", cell->data.index_leaf_cell.payload_info.local_bytes, cell->data.index_leaf_cell.payload_info.overflow_page);
    } else {
        cell->data.index_leaf_cell.payload_info.overflow_page = 0;
    }
}

static void read_index_interior_cell(struct Pager *pager, const uint8_t *data, struct Cell *cell) {
    // Should only be called from read_cell

    // A 4-byte big-endian page number which is the left child pointer.
    // A varint which is the total number of bytes of key payload, including any overflow
    // The initial portion of the payload that does not spill to overflow pages.
    // A 4-byte big-endian integer page number for the first page of the overflow page list - omitted if all payload fits on the b-tree page. 

    uint64_t bytes_read = 4; // For left child pointer

    cell->data.index_interior_cell.left_child_pointer                   = read_u32_big_endian(data, 0);
    cell->data.index_interior_cell.payload_info.payload_size            = read_varint(data, &bytes_read);
    cell->data.index_interior_cell.payload_info.payload_offset          = cell->cell_offset + bytes_read;
    cell->data.index_interior_cell.payload_info.local_bytes             = get_index_cell_payload_local_bytes(pager, cell);

    if (cell->data.index_interior_cell.payload_info.local_bytes < cell->data.index_interior_cell.payload_info.payload_size) {
        cell->data.index_interior_cell.payload_info.overflow_page = read_u32_big_endian(data, bytes_read + cell->data.index_interior_cell.payload_info.local_bytes);
        fprintf(stderr, "Local bytes: %lld, Overflow page: %d\n", cell->data.index_interior_cell.payload_info.local_bytes, cell->data.index_interior_cell.payload_info.overflow_page);
    } else {
        cell->data.index_interior_cell.payload_info.overflow_page = 0;
    }
}

void read_cell(struct Pager *pager, struct PageHeader *page_header, struct Cell *cell, uint16_t cell_offset) {
    struct Page *page = get_page(pager, page_header->page_number);
    uint8_t *data = page->data + cell_offset;
    cell->page_number = page_header->page_number;
    cell->cell_offset = cell_offset;

    switch (page_header->page_type) {
        case PAGE_LEAF_TABLE:
            cell->type = TABLE_LEAF_CELL;
            read_table_leaf_cell(pager, data, cell);
            break;

        case PAGE_INTERIOR_TABLE:
            cell->type = TABLE_INTERIOR_CELL;
            read_table_interior_cell(pager, data, cell);
            break;

        case PAGE_LEAF_INDEX:
            cell->type = INDEX_LEAF_CELL;
            read_index_leaf_cell(pager, data, cell);
            break;

        case PAGE_INTERIOR_INDEX:
            cell->type = INDEX_INTERIOR_CELL;
            read_index_interior_cell(pager, data, cell);
            break;

        default:
            fprintf(stderr, "Unknown page type\n");
            exit(1);
    }

    pager_release_page(page);
}
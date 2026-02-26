#include <stdint.h>
#include <stdlib.h>

#include "byte_reader.h"
#include "page_parsing.h"
#include "../pager.h"

#define INTERIOR_INDEX_BYTE (0x02)
#define INTERIOR_TABLE_BYTE (0x05)
#define LEAF_INDEX_BYTE (0x0A)
#define LEAF_TABLE_BYTE (0x0D)
#define CELL_CONTENT_ZERO (65536)

static enum PageType byte_to_page_type(uint8_t byte) {
    switch(byte) {
        case INTERIOR_INDEX_BYTE:
        return PAGE_INTERIOR_INDEX;

        case INTERIOR_TABLE_BYTE:
        return PAGE_INTERIOR_TABLE;

        case LEAF_INDEX_BYTE:
        return PAGE_LEAF_INDEX;

        case LEAF_TABLE_BYTE:
        return PAGE_LEAF_TABLE;

        default:
        return PAGE_INVALID;
    }
}

void read_page_header(struct Pager *pager, struct PageHeader *page_header, uint32_t page_number) {
    // fprintf(stderr, "Reading page number: %d\n", page_number);
    struct Page *page = get_page(pager, page_number);
    page_header->page_number = page_number;
    uint8_t *data = page->data;

    page_header->start_of_cell_pointer_array = 0;

    if (page_number == 1) { // Skip database header if on the first page
        data += DATABASE_HEADER_SIZE;
        page_header->start_of_cell_pointer_array = DATABASE_HEADER_SIZE;
    }

    page_header->page_type          = byte_to_page_type(data[PAGE_TYPE_OFFSET]);
    page_header->freeblock          = read_u16_big_endian(data, PAGE_FREEBLOCK_OFFSET);
    page_header->number_of_cells    = read_u16_big_endian(data, PAGE_NUMBER_OF_CELLS_OFFSET);
    page_header->cell_content       = read_u16_big_endian(data, PAGE_CELL_CONTENT_OFFSET);

    if (page_header->cell_content == 0) page_header->cell_content = CELL_CONTENT_ZERO; // A zero value for this integer is interpreted as 65536.

    page_header->fragmented_free_bytes = data[PAGE_FRAGMENTED_FREE_BYTES_OFFSET];

    if (page_header->page_type == PAGE_INTERIOR_INDEX || page_header->page_type == PAGE_INTERIOR_TABLE) {
        page_header->header_size = 12;
        // Right most pointer only found in interior b-tree pages
        page_header->right_most_pointer = read_u32_big_endian(data, PAGE_RIGHT_MOST_POINTER_OFFSET);
    } else {
        page_header->right_most_pointer = 0;
        page_header->header_size = 8;
    }

    page_header->start_of_cell_pointer_array += page_header->header_size;

    pager_release_page(page);
}

// @TODO: should maybe get passed a pointer to write to instead of malloc in function
uint16_t *read_cell_pointer_array(struct Pager *pager, struct PageHeader *page_header) {
    struct Page *page = get_page(pager, page_header->page_number);

    if (!page) {
        fprintf(stderr, "get_page failed\n");
        exit(1);
    }

    uint8_t *data = page->data + page_header->start_of_cell_pointer_array;

    uint16_t *offsets = malloc(page_header->number_of_cells * sizeof(uint16_t));

    if (!offsets) {
        fprintf(stderr, "read_cell_pointer_array: offsets malloc failed\n");
        exit(1);
    }

    for (int i = 0; i < page_header->number_of_cells; i++) {
        offsets[i] = read_u16_big_endian(data, i * 2);
    }
    
    pager_release_page(page);
    return offsets;
}

uint16_t *read_page_header_and_cell_pointer_array(struct Pager *pager, struct PageHeader *page_header, uint32_t page_number) {
    read_page_header(pager, page_header, page_number);
    return read_cell_pointer_array(pager, page_header);
}
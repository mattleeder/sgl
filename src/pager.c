#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "pager.h"
#include "data_parsing/byte_reader.h"
#include "data_parsing/page_parsing.h"
#include "sql_utils.h"


static uint32_t read_next_four_bytes_as_big_endian(FILE *file) {
    uint8_t buffer[4];

    if (fread(buffer, 1, 4, file) != 4) {
        fprintf(stderr, "read_next_four_bytes_as_big_endian: fread failed\n");
        exit(1);
    }

    uint32_t number = read_u32_big_endian(buffer, 0);
    return number;
}

static void read_database_header(FILE *file, struct DatabaseHeader *database_header) {
    uint8_t buffer[2];

    // Skip the magic string
    if (fseek(file, MAGIC_STRING_LENGTH, SEEK_SET) != 0) {
        fprintf(stderr, "read_database_header: fseek past magic string failed\n");
        exit(1);
    }

    // Read 2-byte page size
    if (fread(buffer, 1, 2, file) != 2) {
        fprintf(stderr, "read_database_header: fread failed to read page size\n");
        exit(1); 
    }

    database_header->page_size = read_u16_big_endian(buffer, 0);
    if (database_header->page_size == 1) {
        // A size of 1 represents 65536
        database_header->page_size = 65536;
    }

    // Read 1-byte reserved space amount
    if (fseek(file, 2, SEEK_CUR) != 0) {
        fprintf(stderr, "read_database_header: fseek to reserved space failed\n");
        exit(1);    
    }

    if (fread(buffer, 1, 1, file) != 1) {
        fprintf(stderr, "read_database_header: fread failed to read reserved space amount\n");
        exit(1); 
    }
    database_header->reserved_space = buffer[0];

    if (fseek(file, 28, SEEK_SET) != 0) {
        fprintf(stderr, "read_database_header: fseek failed to seek to page count\n");
        exit(1); 
    }
    database_header->page_count = read_next_four_bytes_as_big_endian(file);

    if (fseek(file, 48, SEEK_SET) != 0) {
        fprintf(stderr, "read_database_header: fseek failed to seek to default page cache size\n");
        exit(1);    
    }
    database_header->default_page_cache_size = read_next_four_bytes_as_big_endian(file);
}

struct Pager *pager_open(const char *database_file_path) {
    FILE *database_file = fopen(database_file_path, "rb");
    
    struct DatabaseHeader *database_header = malloc(sizeof(struct DatabaseHeader));
    if (!database_header) {
        fprintf(stderr, "pager_open: *database_header malloc failed\n");
        exit(1);
    }

    read_database_header(database_file, database_header);

    fprintf(stderr, "database_header.page_count: %d\n", database_header->page_count);
    fprintf(stderr, "database_header.page_size: %d\n", database_header->page_size);
    fprintf(stderr, "database_header.reserved_space: %d\n", database_header->reserved_space);
    fprintf(stderr, "default_page_cache_size: %d\n", database_header->default_page_cache_size);

    struct PageHeader *schema_page_header = malloc(sizeof(struct PageHeader));
    struct Pager *pager = malloc(sizeof(struct Pager));

    if (!schema_page_header) {
        fprintf(stderr, "pager_open: *schema_page_header malloc failed\n");
        exit(1);
    }

    if (!pager) {
        fprintf(stderr, "pager_open: *pager malloc failed\n");
        exit(1);
    }

    pager->file                 = database_file;
    pager->page_size            = database_header->page_size;
    pager->page_count           = database_header->page_count;

    pager->cache_capacity       = database_header->default_page_cache_size < MIN_CACHE_CAPACITY ? MIN_CACHE_CAPACITY : database_header->default_page_cache_size;
    pager->pages                = calloc(pager->cache_capacity, sizeof(struct Page));
    pager->data                 = calloc(pager->cache_capacity, pager->page_size);
    pager->clock                = 0;

    pager->database_header      = database_header;
    pager->schema_page_header   = schema_page_header;

    struct Page *page;
    for (int i = 0; i < pager->cache_capacity; i++) {
        page = &pager->pages[i];
        page->data = pager->data + pager->page_size * i;
    }

    read_page_header(pager, schema_page_header, 1);

    return pager;
}

void pager_close(struct Pager *pager) {
    fclose(pager->file);
    free(pager->pages);
    free(pager->data);
    free(pager->database_header);
    free(pager->schema_page_header);
}

static void evict_cache_entry(struct Pager *pager, uint32_t cache_index) {
    struct Page *page = &pager->pages[cache_index];
    page->valid = false;
}

static uint32_t find_suitable_cache_index(struct Pager *pager) {
    bool cache_valid = false;
    uint32_t cache_index = 0;
    uint64_t oldest = UINT64_MAX;

    for (int i = 0; i < pager->cache_capacity; i++) {
        struct Page *page = &pager->pages[i];
        if (!page->valid) {
            return i;
        }

        if (page->pin_count > 0) {
            continue;
        }

        if (page->last_used <= oldest) {
            cache_index = i;
            oldest = page->last_used;
            cache_valid = true;
        }
    }

    if (!cache_valid) {
        fprintf(stderr, "Pager failed to find suitable cache_index.\n");
        exit(1);
    }

    evict_cache_entry(pager, cache_index);
    return cache_index;
}

static void read_new_page(struct Pager *pager, uint32_t page_number, uint32_t cache_index) {
    struct Page *page = &pager->pages[cache_index];

    if (fseek(pager->file, pager->page_size * (page_number - 1), SEEK_SET) != 0) {
        fprintf(stderr, "read_new_page: fseek failed\n");
        exit(1);
    }

    if (fread(page->data, 1, pager->page_size, pager->file) != pager->page_size) {
        fprintf(stderr, "read_new_page: fread failed\n");
        exit(1);
    }

    page->page_no = page_number;
    page->valid = true;
    page->last_used = pager->clock++;
    page->pin_count++;
}

void pager_release_page(struct Page *page) {
    if (page->pin_count > 0) {
        page->pin_count--;
    }
}

struct Page *get_page(struct Pager *pager, uint32_t page_number) {
    // 1. Check cache
    for (int i = 0; i < pager->cache_capacity; i++) {
        struct Page *page = &pager->pages[i];
        if (page->page_no == page_number && page->valid) {
            page->last_used = pager->clock++;
            return page;
        }
    }

    // 2. Not found, find slot
    uint32_t cache_index = find_suitable_cache_index(pager);

    // 3. Load page
    read_new_page(pager, page_number, cache_index);

    return &pager->pages[cache_index];
}
#ifndef sql_page
#define sql_page

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define MIN_CACHE_CAPACITY (16)
#define MAGIC_STRING_LENGTH (16)

struct DatabaseHeader {
    uint8_t     reserved_space;
    uint32_t    page_size;
    uint32_t    page_count;
    uint32_t    default_page_cache_size;
};

struct Page {
    int         pin_count;
    uint32_t    page_no;
    uint8_t     *data;
    bool        valid;
    uint64_t    last_used;
};

struct Pager {
    FILE        *file;
    uint32_t    page_size;
    uint32_t    page_count;

    struct Page *pages;
    uint8_t     *data;
    uint32_t    cache_capacity;

    uint64_t    clock;

    struct DatabaseHeader *database_header;
    struct PageHeader     *schema_page_header;
};

struct Pager *pager_open(const char *database_file_path);
void pager_close(struct Pager *pager);
struct Page *get_page(struct Pager *pager, uint32_t page_number);
void pager_release_page(struct Page *page);

#endif
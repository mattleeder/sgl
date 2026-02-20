#ifndef sql_sql_utils
#define sql_sql_utils

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "memory.h"
#include "pager.h"

#define CACHE_SIZE (16)

struct IndexData {
    struct Columns  *columns;
    uint32_t        root_page;
};

struct Column {
    uint32_t    index;
    size_t      name_length;
    char        *name_start;
};

DEFINE_VECTOR(struct Column, Columns, columns)
DEFINE_VECTOR(struct IndexData, IndexArray, index_array)

struct SchemaRecord *get_schema_record_for_table(struct Pager *pager, char *table_name);
uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, char *column_name_start, size_t column_name_length);

#endif
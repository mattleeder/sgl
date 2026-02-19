#ifndef sql_sql_utils
#define sql_sql_utils

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "ast.h"
#include "pager.h"

#define CACHE_SIZE (16)

struct Columns {
    size_t      count;
    size_t      capacity;
    uint32_t    *indexes;
    size_t      *name_lengths;
    char        **names;
};

struct SchemaRecord *get_schema_record_for_table(struct Pager *pager, char *table_name);
uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, char *column_name);

void init_columns(struct Columns *columns);
void free_columns(struct Columns *columns);
void write_column(struct Columns *columns, uint32_t index, const char *name_start, size_t length);

#endif
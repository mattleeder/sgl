#ifndef sql_sql_utils
#define sql_sql_utils

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "ast.h"
#include "memory.h"
#include "pager.h"

struct IndexColumns {
    struct Columns  *columns;
    uint32_t        root_page;
};

struct IndexData {
    struct Columns          *columns;
    struct BinaryExprList   *predicates;
    uint32_t                root_page;
};

struct Column {
    uint32_t    index;
    size_t      name_length;
    const char  *name_start;
};

static inline size_t hash_column(struct Column column) {
    return hash_djb2_unterminated(column.name_start, column.name_length);
}

static inline bool equals_column(struct Column a, struct Column b) {
    if (a.name_length != b.name_length) {
        return false;
    }

    if (a.name_start == b.name_start) {
        return true;
    }

    return strncmp(a.name_start, b.name_start, a.name_length) == 0;
}

DEFINE_HASH_MAP(struct Column, bool, ColumnToBool, column_to_bool, hash_column, equals_column)

DEFINE_VECTOR(struct Column, Columns, columns)
DEFINE_VECTOR(struct IndexColumns, IndexColumnsArray, index_columns_array)

struct SchemaRecord *get_schema_record_for_table(struct Pager *pager, char *table_name);
uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, char *column_name_start, size_t column_name_length);
struct IndexColumnsArray *get_all_indexes_for_table(struct Pager *pager, char* table_name);

#endif
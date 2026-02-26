#ifndef sql_sql_utils
#define sql_sql_utils

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "ast.h"
#include "memory.h"
#include "pager.h"
#include "common.h"

#include "./utilities/hash_map.h"

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
    uint32_t                    index;
    struct UnterminatedString   name;
};

DEFINE_TYPED_HASH_MAP(struct Column, bool, ColumnToBool, column_to_bool)

DEFINE_VECTOR(struct Column, Columns, columns)
DEFINE_VECTOR(struct IndexColumns, IndexColumnsArray, index_columns_array)

static inline size_t hash_column_ptr(const void *column) {
    return hash_djb2_unterminated(((struct Column *)(column))->name.start, ((struct Column *)(column))->name.len);
}

static inline bool equals_column_ptr(const void *a, const void *b) {
    return unterminated_string_equals(&((struct Column *)(a))->name, &((struct Column *)(b))->name);
}

struct SchemaRecord *get_schema_record_for_table(struct Pager *pager, const char *table_name);
uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, struct UnterminatedString *column_name);
struct IndexColumnsArray *get_all_indexes_for_table(struct Pager *pager, char* table_name);

#endif
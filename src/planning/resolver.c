#include <stdbool.h>

#include "common.h"
#include "../pager.h"
#include "../parser.h"
#include "page_parsing.h"
#include "record_parsing.h"

// Turn column names into row indexes for each stage of the query

DEFINE_HASH_MAP(struct Column, size_t, ColumnToIndex, column_to_index, hash_column, equals_column)

bool get_is_first_col_rowid(struct Column *col) {
    char *buffer[2] = "id";
    struct UnterminatedString id_string = { .start = &buffer, .len = 2};
    return unterminated_string_equals(col, &id_string);
}

void resolve_full_row(struct Pager *pager, struct SelectStatement *stmt) {
    if (!stmt->select_list) {
        return;
    }

    // Is rowid in row
    struct SchemaRecord *schema_record = get_schema_record_for_table(pager, stmt->from_table);
    struct PageHeader page_header;
    read_page_header(pager, &page_header, schema_record->body.root_page);

    struct Parser parser_create;
    // Empty pool, dont care about reserved words here
    struct TriePool *reserved_words_pool = init_reserved_words();

    struct Columns *columns = parse_create(&parser_create, schema_record->body.sql, reserved_words_pool);
    if (columns->count < 0) {
        return;
    }

    bool first_col_is_rowid = get_is_first_col_rowid(&columns->data[0]);

    struct ColumnToIndexHashMap column_to_index;
    hash_map_column_to_index_init(&column_to_index, columns->count, 0.75);

    for (size_t i = 0; i < columns->count; i++) {
        struct Column *column = &columns->data[i];
        hash_map_column_to_index_set(&column_to_index, *column, i);
    }

    
}
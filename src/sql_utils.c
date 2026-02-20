#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "memory.h"
#include "data_parsing/byte_reader.h"
#include "pager.h"
#include "data_parsing/page_parsing.h"
#include "data_parsing/cell_parsing.h"
#include "data_parsing/record_parsing.h"

#include "parser.h"
#include "sql_utils.h"

// @TODO: need to actually use this
bool add_overflow_size_t(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

struct SchemaRecord *get_schema_record_for_table(struct Pager *pager, char *table_name) {
    // Read schema
    int16_t number_of_tables = pager->schema_page_header->number_of_cells;
    uint16_t *schema_offsets = read_cell_pointer_array(pager, pager->schema_page_header);
    fprintf(stderr, "There are %d tables\n", number_of_tables);
    
    struct Cell cell;
    struct SchemaRecord *schema_record = malloc(sizeof(struct SchemaRecord));
    
    if (!schema_record) {
        fprintf(stderr, "get_schema_record_for_table: *schema_record malloc failed\n");
        exit(1);
    }

    for (int i = 0; i < number_of_tables; i++) {

        read_cell_and_schema_record(
            pager,
            pager->schema_page_header,
            &cell,
            schema_offsets[i],
            schema_record
        );

        if (strcmp(table_name, schema_record->body.table_name) != 0) {
            continue;
        }

        return schema_record;
    }

    fprintf(stderr, "Could not find table: %s\n", table_name);
    exit(1);
}

uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, char *column_name_start, size_t column_name_length) {
    // Read schema
    int16_t number_of_tables = pager->schema_page_header->number_of_cells;
    uint16_t *schema_offsets = read_cell_pointer_array(pager, pager->schema_page_header);

    // @TODO: should not be creating new pool here
    struct TriePool *pool = init_reserved_words();

    struct Cell cell;
    struct SchemaRecord schema_record;
    struct Parser parser_create_index;

    for (int i = 0; i < number_of_tables; i++) {

        read_cell_and_schema_record(
            pager,
            pager->schema_page_header,
            &cell,
            schema_offsets[i],
            &schema_record);
        
        // If table name does not match
        if (strcmp(table_name, schema_record.body.table_name) != 0) {
            continue;
        }

        if (strcmp("index", schema_record.body.schema_type) != 0) {
            continue;
        }

        struct CreateIndexStatement *stmt = parse_create_index(&parser_create_index, schema_record.body.sql, pool);

        if(stmt->indexed_columns->count != 1) {
            free_columns(stmt->indexed_columns);
            free(stmt);
            continue;
        }

        struct Column first_column = stmt->indexed_columns->data[0];

        if (column_name_length != first_column.name_length || strncmp(column_name_start, first_column.name_start, first_column.name_length) != 0) {
            free_columns(stmt->indexed_columns);
            free(stmt);
            continue;
        }

        free_columns(stmt->indexed_columns);
        free(stmt);

        return schema_record.body.root_page;
    }

    return (uint32_t)0;
}

struct IndexArray *get_all_indexes_for_table(struct Pager *pager, char* table_name) {
    struct IndexArray *index_array = malloc(sizeof(struct IndexArray));
    if (!index_array) {
        fprintf(stderr, "get_all_indexes_for_table: failed to malloc *index_array.\n");
        exit(1);
    }

    init_index_array(index_array);

    struct PageHeader page_header;
    uint16_t *cell_offsets = read_page_header_and_cell_pointer_array(pager, &page_header, 1);

    // @TODO: should not be creating new pool here
    struct TriePool *pool = init_reserved_words();

    struct Cell cell;
    struct SchemaRecord record;
    struct Parser parser_create_index;

    for (int i = 0; i < page_header.number_of_cells; i++) {
        uint16_t offset = cell_offsets[i];
        read_cell_and_schema_record(pager, &page_header, &cell, offset, &record);

        if (strcmp(record.body.table_name, table_name) != 0) {
            continue;
        }

        if (strcmp(record.body.schema_type, "index") != 0) {
            continue;
        }

        struct CreateIndexStatement *stmt = parse_create_index(&parse_create_index, record.body.sql, pool);

        struct IndexData index_data = { .root_page = record.body.root_page, .columns = stmt->indexed_columns };

        push_index_array(index_array, index_data);
    }

    return index_array;
}
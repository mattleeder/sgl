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
            1,
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

uint32_t get_root_page_of_first_matching_index(struct Pager *pager, char *table_name, char *column_name) {
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
            1,
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

        size_t first_column_name_length = stmt->indexed_columns->name_lengths[0];
        char *first_column_name         = stmt->indexed_columns->names[0];
        if (strncmp(column_name, first_column_name, first_column_name_length) != 0) {
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

static void init_columns(struct Columns *columns) {
    columns->count          = 0;
    columns->capacity       = 0;
    columns->indexes        = NULL;
    columns->name_lengths   = NULL;
    columns->names          = NULL;
}

static void free_columns(struct Columns *columns) {
    FREE_ARRAY(uint32_t, columns->indexes, columns->capacity);
    FREE_ARRAY(size_t, columns->name_lengths, columns->capacity);
    FREE_ARRAY(char*, columns->names, columns->capacity);
    init_columns(columns);
}

static void write_column(struct Columns *columns, uint32_t index, const char *name_start, size_t length) {
    if (columns == NULL) {
        fprintf(stderr, "Writing to NULL struct Columns pointer.\n");
        exit(1);
    }

    if (columns->capacity < columns->count + 1) {
        size_t old_capacity = columns->capacity;
        columns->capacity       = grow_capacity(old_capacity);
        columns->indexes        = GROW_ARRAY(uint32_t, columns->indexes, old_capacity, columns->capacity);
        columns->name_lengths   = GROW_ARRAY(size_t, columns->name_lengths, old_capacity, columns->capacity);
        columns->names          = GROW_ARRAY(char *, columns->names, old_capacity, columns->capacity);
    }

    columns->indexes[columns->count]        = index;
    columns->name_lengths[columns->count]   = length;
    columns->names[columns->count]          = name_start;
    columns->count++;
}
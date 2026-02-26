#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "byte_reader.h"
#include "row_parsing.h"
#include "cell_parsing.h"
#include "record_parsing.h"

void free_row(struct Row *row) {
    free(row->values);
}

static void decode_column(uint8_t *data, struct ContentType type, struct Value *value) {

    switch (type.type_name) {

        case SQL_NULL:
            value->type                 = VALUE_NULL;
            value->null_value.null_ptr  = NULL;
            break;

        case SQL_8INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int8_t)data[0];
            break;

        case SQL_16INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int16_t)read_u16_big_endian(data, 0);
            break;

        case SQL_24INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int32_t)read_u24_big_endian(data, 0);
            break;

        case SQL_32INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int32_t)read_u32_big_endian(data, 0);
            break;

        case SQL_48INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int64_t)read_u48_big_endian(data, 0);
            break;

        case SQL_64INT:
            value->type                 = VALUE_INT;
            value->int_value.value      = (int64_t)read_u64_big_endian(data, 0);
            break;

        case SQL_64FLOAT:
            fprintf(stderr, "Should not be decoding SQL_RESERVED.\n");
            exit(1);    

        case SQL_0:
            value->type             = VALUE_INT;
            value->int_value.value  = 0;
            break;

        case SQL_1:
            value->type             = VALUE_INT;
            value->int_value.value  = 1;
            break;

        case SQL_RESERVED:
            fprintf(stderr, "Should not be decoding SQL_RESERVED.\n");
            exit(1);
            
        case SQL_BLOB:
            fprintf(stderr, "SQL_BLOB not supported yet.\n");
            exit(1);
            
        case SQL_STRING:
            value->type             = VALUE_TEXT;
            value->text_value.data  = data;
            value->text_value.len   = type.content_size;
            break;
            
        case SQL_INVALID:
            fprintf(stderr, "Should not be decoding SQL_INVALID.\n");
            exit(1);

        default:
            fprintf(stderr, "decode_column: Unknown type %d.\n", type.type_name);
            exit(1);
            
    }
}

void read_row_from_record(struct Record *record, struct Row *row, struct Cell *cell) {
    row->column_count   = record->header.number_of_columns;
    row->values         = malloc(record->header.number_of_columns * sizeof(struct Value));

    if (!row->values) {
        fprintf(stderr, "read_row_from_record: row->values malloc failed\n");
        exit(1);
    }

    for (int i = 0; i < record->header.number_of_columns; i++) {
        decode_column(record->body.column_pointers[i], record->header.columns[i], &row->values[i]);
    }

    switch (cell->type) {
        case PAGE_LEAF_TABLE:
            row->rowid = cell->data.table_leaf_cell.row_id;
            break;

        case PAGE_INTERIOR_INDEX:
        case PAGE_LEAF_INDEX:
            row->rowid = row->values[0].int_value.value;
            break;
        
        default:
            fprintf(stderr, "Cannot read record using page type: %d\n", cell->type);
            exit(1);
    }
}

void read_cell_offset_into_row(struct Pager *pager, struct Row *row, struct PageHeader *page_header, uint16_t cell_offset) {
    struct Cell cell;
    struct Record record;

    read_cell_and_record(
        pager,
        page_header,
        &cell,
        cell_offset,
        &record
    );

    read_row_from_record(&record, row, &cell);
}

void print_value(struct Value *value) {
    switch (value->type) {

        case VALUE_NULL:
            printf("NULL");
            break;

        case VALUE_INT:
            printf("%lld", value->int_value.value);
            break;

        case VALUE_FLOAT:
            printf("%f", value->float_value.value);
            break;

        case VALUE_TEXT:
            printf("%.*s", (int)value->text_value.len, value->text_value.data);
            break;

        default:
            fprintf(stderr, "print_value: Unknown Value: %d\n", value->type);
            exit(1);
    }
}

void print_value_to_stderr(struct Value *value) {
    if (value == NULL) {
        fprintf(stderr, "struct Value pointer is NULL.\n");
        exit(1);
    }

    switch (value->type) {

        case VALUE_NULL:
            fprintf(stderr, "NULL");
            break;

        case VALUE_INT:
            fprintf(stderr, "%lld", value->int_value.value);
            break;

        case VALUE_FLOAT:
            fprintf(stderr, "%f", value->float_value.value);
            break;

        case VALUE_TEXT:
            fprintf(stderr, "%.*s", (int)value->text_value.len, value->text_value.data);
            break;

        default:
            fprintf(stderr, "print_value_to_stderr: Unknown Value: %d\n", value->type);
            exit(1);
    }
}

void print_row(struct Row *row) {
    for (int i = 0; i < row->column_count; i++) {
        if (i > 0) {
            printf("|");
        }

        print_value(&row->values[i]);
    }

    printf("\n");
}

void print_row_to_stderr(struct Row *row) {
    if (row == NULL) {
        fprintf(stderr, "print_row_to_stderr: struct Row pointer is NULL.\n");
        exit(1);
    }

    for (int i = 0; i < row->column_count; i++) {
        if (i > 0) {
            fprintf(stderr, "|");
        }

        print_value_to_stderr(&row->values[i]);
    }

    fprintf(stderr, "\n");
}
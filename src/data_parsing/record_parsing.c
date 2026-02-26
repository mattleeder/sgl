#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "byte_reader.h"
#include "../pager.h"
#include "page_parsing.h"
#include "cell_parsing.h"
#include "record_parsing.h"
#include "row_parsing.h"

struct PayloadBuffer {
    uint8_t *data;
    size_t  size;
};

// @TODO: does this overlap with another function?
static uint64_t read_root_page(uint8_t *data, uint64_t serial_type) {
    switch(serial_type) {
        case 1: return (uint64_t)((int8_t)data[0]);
        case 2: return (uint64_t)read_u16_big_endian(data, 0);
        case 3: return (uint64_t)read_u24_big_endian(data, 0);
        case 4: return (uint64_t)read_u32_big_endian(data, 0);
        case 5: return (uint64_t)read_u48_big_endian(data, 0);
        case 6: return (uint64_t)read_u64_big_endian(data, 0);
        case 8: return (uint64_t)0;
        case 9: return (uint64_t)1;
        default: fprintf(stderr, "Unexpected rootpage serial type\n"); exit(1);
    }
}

static struct PayloadInfo get_payload_info_from_cell(struct Cell *cell) {
    struct PayloadInfo payload_info;

    switch (cell->type) {

        case TABLE_LEAF_CELL:
            payload_info = cell->data.table_leaf_cell.payload_info;
            break;

        case INDEX_LEAF_CELL:
            payload_info = cell->data.index_leaf_cell.payload_info;
            break;

        case INDEX_INTERIOR_CELL:
            payload_info = cell->data.index_interior_cell.payload_info;
            break;

        default:
            fprintf(stderr, "Cell type %d does not have payload_info\n", cell->type);
            exit(1);

    }

    return payload_info;
}

// @TODO: make use of overflow page struct
static void read_payload_overflow(struct Pager *pager, struct PayloadInfo *payload_info, uint8_t *data_buffer) {
    uint64_t bytes_read     = payload_info->local_bytes;
    uint32_t overflow_page  = payload_info->overflow_page;
    uint64_t usable_size    = pager->page_size - pager->database_header->reserved_space;
    uint64_t overflow_capacity = usable_size - 4;

    while (bytes_read < payload_info->payload_size && overflow_page != 0) {
        struct Page *page = get_page(pager, overflow_page);
        overflow_page = read_u32_big_endian(page->data, 0); // First 4-bytes points to next overflow page
        
        uint64_t bytes_to_read = payload_info->payload_size - bytes_read;
        bytes_to_read = bytes_to_read > overflow_capacity ? overflow_capacity : bytes_to_read;
        memcpy(data_buffer + bytes_read, page->data + 4, bytes_to_read);
        bytes_read += bytes_to_read;
        pager_release_page(page);
    }
}

static struct PayloadBuffer read_full_payload(struct Pager *pager, struct Cell *cell) {
    struct Page *page = get_page(pager, cell->page_number);
    struct PayloadBuffer payload_buffer;
    struct PayloadInfo payload_info = get_payload_info_from_cell(cell);
    
    uint32_t header_offset = payload_info.payload_offset;
    uint8_t *data = page->data + header_offset;

    payload_buffer.data = malloc(payload_info.payload_size);
    payload_buffer.size = payload_info.payload_size;

    if (!payload_buffer.data) {
        fprintf(stderr, "read_full_payload: failed to malloc payload_buffer.data\n");
        exit(1);
    }

    memcpy(payload_buffer.data, data, payload_info.payload_size);
    
    if (payload_info.overflow_page != 0) {
        read_payload_overflow(pager, &payload_info, payload_buffer.data);
    }

    pager_release_page(page);
    return payload_buffer;
}

static void free_payload_buffer(struct PayloadBuffer *payload_buffer) {
    free(payload_buffer->data);
    payload_buffer->data = NULL;
    payload_buffer->size = 0;
}

static struct ContentType serial_type_to_content_type(uint64_t serial_type) {
    struct ContentType content_type;

    switch(serial_type) {
        case 0:                 // Value is a NULL. 
        // fprintf(stderr, "Read NULL column\n");
        content_type.type_name      = SQL_NULL;
        content_type.content_size   = 0;
        break;

        case 1:                 // Value is an 8-bit twos-complement integer. 
        content_type.type_name      = SQL_8INT;
        content_type.content_size   = 1;
        break;

        case 2:                 // Value is a big-endian 16-bit twos-complement integer. 
        content_type.type_name      = SQL_16INT;
        content_type.content_size   = 2;
        break;

        case 3:                 // 	Value is a big-endian 24-bit twos-complement integer. 
        content_type.type_name      = SQL_24INT;
        content_type.content_size   = 3;
        break;

        case 4:                 // Value is a big-endian 32-bit twos-complement integer. 
        content_type.type_name      = SQL_32INT;
        content_type.content_size   = 4;
        break;

        case 5:                 // Value is a big-endian 48-bit twos-complement integer. 
        content_type.type_name      = SQL_48INT;
        content_type.content_size   = 6;
        break;

        case 6:                 // Value is a big-endian 64-bit twos-complement integer. 
        content_type.type_name      = SQL_64INT;
        content_type.content_size   = 8;
        break;

        case 7:                 // Value is a big-endian IEEE 754-2008 64-bit floating point number. 
        content_type.type_name      = SQL_64FLOAT;
        content_type.content_size   = 8;
        break;

        case 8:                 // Value is the integer 0. (Only available for schema format 4 and higher.) 
        content_type.type_name      = SQL_0;
        content_type.content_size   = 0;
        break;;

        case 9:                 // 	Value is the integer 1. (Only available for schema format 4 and higher.) 
        content_type.type_name      = SQL_1;
        content_type.content_size   = 0;
        break;

        case 10:                // Reserved for internal use. These serial type codes will never appear in a well-formed database file, but they might be used in transient and temporary database files that SQLite sometimes generates for its own use. The meanings of these codes can shift from one release of SQLite to the next.
        fprintf(stderr, "Invalid serial type %zu\n", serial_type);
        exit(1);
        content_type.type_name      = SQL_INVALID;
        content_type.content_size   = 0;
        break;

        case 11:                // See 10.
        fprintf(stderr, "Invalid serial type %zu\n", serial_type);
        exit(1);
        content_type.type_name      = SQL_INVALID;
        content_type.content_size   = 0;
        break;

        default:
        if (serial_type % 2 == 0) {     // Value is a BLOB that is (N-12)/2 bytes in length. 
            content_type.type_name      = SQL_BLOB;
            content_type.content_size   = (serial_type - 12) / 2;
        } else {                        // Value is a string in the text encoding and (N-13)/2 bytes in length. The nul terminator is not stored. 
            content_type.type_name      = SQL_STRING;
            content_type.content_size   = (serial_type - 13) / 2;
        }
        break;
    }

    return content_type;
}

// @TODO: maybe its better to pass payload info as an arg, will need to pass page_number too tho
static void read_record_header(struct RecordHeader *record_header, struct PayloadBuffer *payload_buffer) {
    uint8_t *data = payload_buffer->data;
    
    uint64_t row_size = 0;
    uint64_t bytes_read = 0;
    
    record_header->header_size  = read_varint(data, &bytes_read);
    
    // Use number of bytes as upper bound on malloc size
    uint64_t max_columns = record_header->header_size - bytes_read;
    struct ContentType *column_types = malloc(max_columns * sizeof(struct ContentType));
    if (!column_types) {
        fprintf(stderr, "read_record_header: *column_types malloc failed\n");
        exit(1);
    }
    
    // @TODO: handle overflow
    
    uint64_t column_number = 0;
    while (bytes_read < record_header->header_size) {
        assert(column_number < max_columns);
        column_types[column_number] = serial_type_to_content_type(read_varint(data, &bytes_read));
        row_size += column_types[column_number].content_size;
        column_number++;
    }

    record_header->row_size             = row_size;
    record_header->number_of_columns    = column_number;
    record_header->columns              = column_types;

    if (bytes_read != record_header->header_size) {
        fprintf(stderr, "Read %lld columns\n", column_number);
        fprintf(stderr, "read_record_header: Header size %lld does not match bytes read %lld\n", record_header->header_size, bytes_read);
        exit(1);
    }
}

static void read_record_body(struct RecordBody *record_body, struct RecordHeader *record_header, struct PayloadBuffer *payload_buffer) {
    // @TODO: maybe its better to pass payload info as an arg, will need to pass page_number too tho

    uint8_t *data = payload_buffer->data + record_header->header_size;
    size_t record_block_size = record_header->row_size + record_header->number_of_columns;

    char **column_pointers      = malloc(record_header->number_of_columns * sizeof(char *));
    char *record_block          = malloc(record_block_size);

    if (!column_pointers) {
        fprintf(stderr, "read_record_body: **column_pointers malloc failed\n");
        exit(1);
    }

    if (!record_block) {
        fprintf(stderr, "read_record_body: *record_block malloc failed\n");
        exit(1);
    }

    record_body->data_block         = record_block;
    record_body->column_pointers    = column_pointers;

    uint64_t column_size = 0;
    for (uint64_t i = 0; i < record_header->number_of_columns; i++) {
        column_size = record_header->columns[i].content_size;
        column_pointers[i] = record_block;

        memcpy(record_block, data, column_size);
        data += column_size;
        record_block[column_size] = '\0';
        record_block += column_size + 1;
    }
}

static void free_schema_record_body(struct SchemaRecordBody *schema_record_body) {
    free(schema_record_body->data_block);
    schema_record_body->data_block = NULL;
}

void free_schema_record(struct SchemaRecord *schema_record) {
    free_schema_record_body(&schema_record->body);
}

static void free_record_body(struct RecordBody *record_body) {
    free(record_body->column_pointers);
    free(record_body->data_block);

    record_body->column_pointers = NULL;
    record_body->data_block = NULL;
}

void free_record(struct Record *record) {
    free_record_body(&record->body);
}

static void read_record(struct Pager *pager, struct Cell *cell, struct Record *record) {
    struct PayloadBuffer payload_buffer = read_full_payload(pager, cell);
    read_record_header(&record->header, &payload_buffer);
    read_record_body(&record->body, &record->header, &payload_buffer);
    free_payload_buffer(&payload_buffer);
}

static void interpret_record_header_as_schema_record_header(struct RecordHeader *record_header, struct SchemaRecordHeader *schema_record_header) {
    assert(record_header->number_of_columns == 5);
    
    schema_record_header->header_size   = record_header->header_size;
    schema_record_header->schema_type   = record_header->columns[0];
    schema_record_header->schema_name   = record_header->columns[1];
    schema_record_header->table_name    = record_header->columns[2];
    schema_record_header->root_page     = record_header->columns[3];
    schema_record_header->sql           = record_header->columns[4];
}

static void interpret_record_body_as_schema_record_body(struct RecordBody *record_body, struct SchemaRecordBody *schema_record_body, struct SchemaRecordHeader *schema_record_header) {
   
    schema_record_body->schema_type         = record_body->column_pointers[0];
    schema_record_body->schema_name         = record_body->column_pointers[1];
    schema_record_body->table_name          = record_body->column_pointers[2];
    schema_record_body->root_page_bytes     = (uint8_t *)record_body->column_pointers[3];
    schema_record_body->sql                 = record_body->column_pointers[4];
    schema_record_body->data_block          = record_body->data_block;
    schema_record_body->root_page           = read_root_page(schema_record_body->root_page_bytes, schema_record_header->root_page.content_size);
}

static void interpret_record_as_schema_record(struct Record *record, struct SchemaRecord *schema) {
    interpret_record_header_as_schema_record_header(&record->header, &schema->header);
    interpret_record_body_as_schema_record_body(&record->body, &schema->body, &schema->header);
}

static void read_schema_record(struct Pager *pager, struct Cell *cell, struct SchemaRecord *schema) {
    struct Record record;
    read_record(pager, cell, &record);
    interpret_record_as_schema_record(&record, schema);
}

void read_cell_and_record(struct Pager *pager,
    struct PageHeader *page_header,
    struct Cell *cell,
    uint16_t cell_offset,
    struct Record *record) 
{
    read_cell(pager, page_header, cell, cell_offset);
    read_record(pager, cell, record);
}

void read_cell_and_schema_record(struct Pager *pager,
    struct PageHeader *page_header,
    struct Cell *cell,
    uint16_t cell_offset,
    struct SchemaRecord *schema_record) 
{
    struct Record record;
    read_cell_and_record(pager, page_header, cell, cell_offset, &record);
    interpret_record_as_schema_record(&record, schema_record);
}
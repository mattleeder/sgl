#ifndef sql_record_parsing
#define sql_record_parsing

#include <stdint.h>
#include <stdbool.h>

#define SCHEMA_COLUMN_COUNT (5)

enum ContentTypeName {
    SQL_NULL,
    SQL_8INT,
    SQL_16INT,
    SQL_24INT,
    SQL_32INT,
    SQL_48INT,
    SQL_64INT,
    SQL_64FLOAT,
    SQL_0,
    SQL_1,
    SQL_RESERVED,
    SQL_BLOB,
    SQL_STRING,
    SQL_INVALID
};



struct ContentType {
    enum ContentTypeName    type_name;
    uint64_t                content_size;
};

struct SchemaRecordHeader {
    uint64_t            header_size;
    struct ContentType  schema_type;
    struct ContentType  schema_name;
    struct ContentType  table_name;
    struct ContentType  root_page;
    struct ContentType  sql;
};

struct SchemaRecordBody {
    char        *schema_type;
    char        *schema_name;
    char        *table_name;
    char        *root_page_bytes;
    char        *sql;
    char        *data_block;
    uint32_t    root_page;
};

struct RecordHeader {
    uint64_t                header_size;
    uint64_t                row_size;
    uint64_t                number_of_columns;
    struct ContentType      *columns;
};

struct RecordBody {
    char    **column_pointers;
    char    *data_block;
};

struct Record {
    struct RecordHeader header;
    struct RecordBody   body;
};

struct SchemaRecord {
    struct SchemaRecordHeader   header;
    struct SchemaRecordBody     body;
};

void free_schema_record(struct SchemaRecord *schema_record);
void free_record(struct Record *record);

void read_cell_and_record(struct Pager *pager,
    struct PageHeader *page_header,
    struct Cell *cell,
    uint16_t cell_offset,
    struct Record *record);

void read_cell_and_schema_record(struct Pager *pager,
    struct PageHeader *page_header,
    struct Cell *cell,
    uint16_t cell_offset,
    struct SchemaRecord *schema_record);

#endif sql_record_parsing
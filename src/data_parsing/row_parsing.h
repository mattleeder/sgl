#ifndef sql_row_parsing
#define sql_row_parsing

#include <stdint.h>

#include "../common.h"
#include "../pager.h"
#include "page_parsing.h"
#include "cell_parsing.h"
#include "record_parsing.h"

enum ValueType {
    VALUE_NULL,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_TEXT
};

struct NullValue {
    void* null_ptr;
};

struct IntValue {
    int64_t value;
};

struct FloatValue {
    float value;
};

struct TextValue {
    struct UnterminatedString text;
};

struct Value {
    enum ValueType type;

    union {
        struct NullValue    null_value;
        struct IntValue     int_value;
        struct FloatValue   float_value;
        struct TextValue    text_value;
    };
};

struct Row {
    uint64_t        rowid;
    uint64_t        column_count;
    struct Value    *values;
};

void free_row(struct Row *row);

void read_row_from_record(struct Record *record, struct Row *row, struct Cell *cell);

void read_cell_offset_into_row(
    struct Pager *pager, 
    struct Row *row, 
    struct PageHeader *page_header, 
    uint16_t cell_offset);
    
void print_value(struct Value *value);
void print_value_to_stderr(struct Value *value);
void print_row(struct Row *row);
void print_row_to_stderr(struct Row *row);

#endif
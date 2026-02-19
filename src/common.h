#ifndef sql_common
#define sql_common

enum OpCode {

    // Table & Cursor OPs
    OP_OPENREAD,     // open table, create cursor
    OP_REWIND,       // move cursor to first row
    OP_NEXT,         // advance cursor
    OP_CLOSE,        // close cursor
    OP_COLUMN,

    OP_RESULTROW,    // emit one result row

    OP_ENDOFSTATEMENT,
};

#endif
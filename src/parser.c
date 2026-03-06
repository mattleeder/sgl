#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#include "token.h"
#include "lexer.h"
#include "trie.h"
#include "common.h"
#include "parser.h"
#include "ast.h"
#include "sql_utils.h"
#include "memory.h"
#include "arena.h"

static const uint8_t char_to_decimal[128] = {
    ['0'] = 0,
    ['1'] = 1,
    ['2'] = 2,
    ['3'] = 3,
    ['4'] = 4,
    ['5'] = 5,
    ['6'] = 6,
    ['7'] = 7,
    ['8'] = 8,
    ['9'] = 9,
    ['A'] = 10,
    ['B'] = 11,
    ['C'] = 12,
    ['D'] = 13,
    ['E'] = 14,
    ['F'] = 15,
    ['a'] = 10,
    ['b'] = 11,
    ['c'] = 12,
    ['d'] = 13,
    ['e'] = 14,
    ['f'] = 15,
};

void parser_init(struct Parser *parser, size_t arena_capacity) {
    parser->head    = 0;
    parser->count   = 0;
    parser->arena   = arena_new(arena_capacity);
}

static struct Token *previous_token(struct Parser *parser) {
    return &parser->previous;
}

static void error_at(struct Parser *parser, struct Token *token, const char *message) {
    parser->panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser->had_error = true;
    exit(1);
}

static void error_at_current(struct Parser *parser, const char* message) {
    error_at(parser, &parser->current, message);
}

static void error(struct Parser *parser, const char* message) {
    error_at(parser, previous_token(parser), message);
}

static inline bool is_buffer_empty(struct Parser *parser) {
    return parser->count == 0;
}

static inline bool is_buffer_full(struct Parser *parser) {
    return parser->count == TOKEN_BUFFER_SIZE;
}

static struct Token consume_next_token_from_buffer(struct Parser *parser) {
    assert(!is_buffer_empty(parser));
    size_t idx = parser->head;
    parser->head = (parser->head + 1) % TOKEN_BUFFER_SIZE;
    parser->count--;
    return parser->buffer[idx];
}

static void write_token_into_buffer(struct Parser *parser, struct Token token) {
    assert(!is_buffer_full(parser));
    size_t idx = (parser->head + parser->count) % TOKEN_BUFFER_SIZE;
    parser->buffer[idx] = token;
    parser->count++;
}

static void read_n_tokens_into_buffer(struct Parser *parser, struct Scanner *scanner, size_t n) {
    assert(n <= TOKEN_BUFFER_SIZE - parser->count);

    for (size_t i = 0; i < n;) {
        struct Token next_token = scan_token(scanner);
        if (next_token.type != TOKEN_ERROR) {
            write_token_into_buffer(parser, next_token);
            i++;
        } else {
            error_at_current(parser, parser->current.start);
        }

    }
}

static void ensure_buffer_has_n_tokens(struct Parser *parser, struct Scanner *scanner, size_t n) {
    assert(n <= TOKEN_BUFFER_SIZE);

    if (parser->count >= n) {
        return;
    }

    size_t diff = n - parser->count;
    read_n_tokens_into_buffer(parser, scanner, diff);
}

static struct Token *peek(struct Parser *parser, struct Scanner *scanner, size_t n) {
    ensure_buffer_has_n_tokens(parser, scanner, n);
    size_t idx = (parser->head + n - 1) % TOKEN_BUFFER_SIZE;
    return &parser->buffer[idx];
}

static struct ExprList *parse_comma_separated_expression_list(struct Parser *parser, struct Scanner *scanner);

static char *get_token_string(struct Token *token) {
    char *buffer = malloc(token->length + 1);
    if (!buffer) {
        fprintf(stderr, "get_token_string: *buffer malloc failed\n");
        exit(1);
    }
    sprintf(buffer, "%.*s", token->length, token->start);
    return buffer;
}

static struct Expr *new_expr(enum ExprType type) {
    struct Expr *expr = malloc(sizeof(struct Expr));
    if (!expr) {
        fprintf(stderr, "new_expr: *expr malloc failed\n");
        exit(1);
    }
    expr->type = type;
    return expr;
}

static struct Expr *make_column_expr(const char *start, size_t len) {
    struct Expr *expr = new_expr(EXPR_COLUMN);
    expr->column.idx            = 0; // To be resolved later
    expr->column.name.start     = start;
    expr->column.name.len       = len;
    return expr;
}

static struct Expr *make_function_expr(char *name, struct ExprList *args) {
    struct Expr *expr = new_expr(EXPR_FUNCTION);
    expr->function.name         = name;
    expr->function.args         = args;
    return expr;
}

static struct Expr *make_star_expr() {
    struct Expr *expr = new_expr(EXPR_STAR);
    return expr;
}

static struct Expr *make_string_expr(struct Parser *parser) {
    struct Expr *expr   = new_expr(EXPR_STRING);
    // Cut off start and end quotes
    expr->string.string.start  = parser->current.start + 1;
    expr->string.string.len    = parser->current.length - 2;
    fprintf(stderr, "Made String Expr: %.*s\n", (int)expr->string.string.len, expr->string.string.start);
    return expr;
}

static struct Expr *make_binary_expr(enum BinaryOp op, struct Expr* left_expr) {
    struct Expr *expr   = new_expr(EXPR_BINARY);
    expr->binary.op     = op;
    expr->binary.left   = left_expr;
    expr->binary.right  = NULL; // Will add later
    return expr;
}

static struct SelectStatement *make_select_statement(
    char *from_table, 
    struct ExprList *select_list,
    struct ExprList *where_list
) {
    struct SelectStatement *stmt = malloc(sizeof(struct SelectStatement));
    if (!stmt) {
        fprintf(stderr, "make_select_statement: *stmt malloc failed\n");
        exit(1);
    }

    stmt->from_table    = from_table;
    stmt->select_list   = select_list;
    stmt->where_list    = where_list;
    return stmt;
}

static void advance(struct Parser *parser, struct Scanner *scanner) {
    parser->previous = parser->current;

    for (;;) {
        if (!is_buffer_empty(parser)) {
            parser->current = consume_next_token_from_buffer(parser);
        } else {
            parser->current = scan_token(scanner);
        }
        if (parser->current.type != TOKEN_ERROR) break;

        error_at_current(parser, parser->current.start);
    }
}


static void consume(struct Parser *parser, struct Scanner *scanner, enum TokenType type, const char *message) {
    if (parser->current.type == type) {
        advance(parser, scanner);
        return;
    }

    error_at_current(parser, message);
}

static struct Expr *parse_function_call(struct Parser *parser, struct Scanner *scanner, char *function_name) {
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    struct ExprList *args = NULL;

    if (parser->current.type != TOKEN_RIGHT_PAREN) {
        args = parse_comma_separated_expression_list(parser, scanner);
    }
    
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'");
    return make_function_expr(function_name, args);
}

static struct Expr *parse_term(struct Parser *parser, struct Scanner *scanner) {
    switch (parser->current.type) {

        case TOKEN_STAR:
            advance(parser, scanner);
            return make_star_expr();

        case TOKEN_IDENTIFIER:
            advance(parser, scanner);

            // Look ahead to decide meaning
            if (parser->current.type == TOKEN_LEFT_PAREN) {
                char *function_name = get_token_string(previous_token(parser));
                return parse_function_call(parser, scanner, function_name);
            }

            struct Token *previous = previous_token(parser);
            return make_column_expr(previous->start, previous->length);

        case TOKEN_STRING:
            return make_string_expr(parser);

        default:
            error_at_current(parser, "Expected expression.");
    }

    return NULL;
}

static struct Expr *parse_expression(struct Parser *parser, struct Scanner *scanner) {
    const char *start = scanner->start;
    struct Expr *expr_left = parse_term(parser, scanner);
    struct Expr *binary_expr = NULL;

    switch (parser->current.type) {

        case TOKEN_EQUAL:
            binary_expr = make_binary_expr(BIN_EQUAL, expr_left);
            break;

        case TOKEN_LESS:
            binary_expr = make_binary_expr(BIN_LESS, expr_left);
            break;

        case TOKEN_GREATER:
            binary_expr = make_binary_expr(BIN_GREATER, expr_left);
            break;

        default: {
            struct Token *previous = previous_token(parser);
            expr_left->text = (struct UnterminatedString){ .start = start, .len = (size_t)(previous->start + previous->length - start)};
            return expr_left;
        }
            
    }

    advance(parser, scanner);
    struct Expr *expr_right = parse_term(parser, scanner);

    binary_expr->binary.right = expr_right;
    struct Token *previous = previous_token(parser);
    binary_expr->text = (struct UnterminatedString){ .start = start, .len = (size_t)(previous->start + previous->length - start)};
    return binary_expr;
}

static struct ExprList *parse_expression_list(struct Parser *parser, struct Scanner *scanner, enum TokenType separator) {
    struct ExprList *expr_list = vector_expr_list_new();

    while (true) {
        struct Expr *expr = parse_expression(parser, scanner);
        vector_expr_list_push(expr_list, *expr);

        
        if (parser->current.type != separator) {
            break;
        }

        advance(parser, scanner);
    }

    return expr_list;
}

static struct ExprList *parse_comma_separated_expression_list(struct Parser *parser, struct Scanner *scanner) {
    return parse_expression_list(parser, scanner, TOKEN_COMMA);
}

static struct ExprList *parse_and_separated_expression_list(struct Parser *parser, struct Scanner *scanner) {
    return parse_expression_list(parser, scanner, TOKEN_AND);
}

static struct SelectStatement *parse_select(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_SELECT, "Expected 'SELECT'.");
    struct ExprList *select_expr_list = parse_comma_separated_expression_list(parser, scanner);

    consume(parser, scanner, TOKEN_FROM, "Expected 'FROM'");

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected 'Table Identifier'.");

    char *from_table = get_token_string(previous_token(parser));

    struct ExprList *where_expr_list = NULL;
    if (parser->current.type == TOKEN_WHERE) {
        advance(parser, scanner);
        where_expr_list = parse_comma_separated_expression_list(parser, scanner);
        fprintf(stderr, "Where has %d expression\n", (int)where_expr_list->count);
    }
    
    return make_select_statement(
        from_table,
        select_expr_list,
        where_expr_list
    );
}

struct SelectStatement *parse(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool) {

    struct Scanner scanner;

    init_scanner(&scanner, source, reserved_words_pool);

    advance(parser, &scanner);
    struct SelectStatement *select_stmt = parse_select(parser, &scanner);

    return select_stmt;
}

struct Columns *parse_create(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool) {
    fprintf(stderr, "%s\n", source);
    struct Columns *columns = vector_columns_new();

    struct Scanner scanner;

    init_scanner(&scanner, source, reserved_words_pool);

    advance(parser, &scanner);

    consume(parser, &scanner, TOKEN_CREATE, "Expected 'CREATE'.");
    consume(parser, &scanner, TOKEN_TABLE, "Expected 'TABLE'.");
    if (parser->current.type != TOKEN_STRING && parser->current.type != TOKEN_IDENTIFIER) {
        error_at_current(parser, "Expected table name.");
    }
    advance(parser, &scanner);
    consume(parser, &scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    
    uint32_t index = 0;
    while(parser->current.type != TOKEN_EOF) {
        
        if (parser->current.type != TOKEN_STRING && parser->current.type != TOKEN_IDENTIFIER) {
            error_at_current(parser, "Expected columnName.");
        }
        
        struct Column column = { .index = index, .name = { .start = parser->current.start, .len = parser->current.length } };
        vector_columns_push(columns, column);
        index++;
        
        advance(parser, &scanner);
        
        while (parser->current.type != TOKEN_EOF && parser->current.type != TOKEN_COMMA) {
            advance(parser, &scanner);
        }
        
        if (parser->current.type == TOKEN_COMMA) {
            advance(parser, &scanner);
        }
    }
    
    return columns;
}

static struct Columns *parse_indexed_column(struct Parser *parser, struct Scanner *scanner) {
    struct Columns *columns = vector_columns_new();

    if (parser->current.type != TOKEN_IDENTIFIER) {
        fprintf(stderr, "Expected column-name.");
        exit(1);
    }

    // @TODO: .index = 0?
    struct Column column = { .index = 0, .name = { .start = parser->current.start, .len = parser->current.length } };
    vector_columns_push(columns, column);
    advance(parser, scanner);

    print_unterminated_string_to_stderr(&column.name);

    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Expected column-name.");
            exit(1);
        }

        column.index        = 0;
        column.name.start   = parser->current.start;
        column.name.len     = parser->current.length;

        vector_columns_push(columns, column);
        advance(parser, scanner);

        print_unterminated_string_to_stderr(&column.name);
    }

    return columns;
}

struct CreateIndexStatement *parse_create_index(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool) {
    struct CreateIndexStatement *stmt = malloc(sizeof(struct CreateIndexStatement));
    if (!stmt) {
        fprintf(stderr, "parse_create_index: *stmt malloc failed\n");
        exit(1);
    }

    fprintf(stderr, "%s\n", source);
    struct Scanner scanner;
    init_scanner(&scanner, source, reserved_words_pool);

    advance(parser, &scanner);

    consume(parser, &scanner, TOKEN_CREATE, "Expected 'CREATE'.");
    

    if (parser->current.type == TOKEN_UNIQUE) {
        advance(parser, &scanner);
    }

    consume(parser, &scanner, TOKEN_INDEX, "Expected 'INDEX'.");


    if (parser->current.type == TOKEN_IF) {
        advance(parser, &scanner);
    }

    if (parser->current.type == TOKEN_NOT) {
        advance(parser, &scanner);
    }

    if (parser->current.type == TOKEN_EXISTS) {
        advance(parser, &scanner);
    }

    if (parser->current.type != TOKEN_IDENTIFIER) {
        fprintf(stderr, "Expected schema-name or index-name.");
        exit(1);
    }


    // Could be schema name, read as index name for now
    stmt->index_name.start  = parser->current.start;
    stmt->index_name.len    = parser->current.length;

    advance(parser, &scanner);

    // Previous was schema name
    if (parser->current.type == TOKEN_DOT) {
        advance(parser, &scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Expected index-name.");
            exit(1);
        }

        stmt->index_name.start  = parser->current.start;
        stmt->index_name.len    = parser->current.length;
    }

    consume(parser, &scanner, TOKEN_ON, "Expected 'ON'.");

    if (parser->current.type != TOKEN_IDENTIFIER) {
        fprintf(stderr, "Expected table-name.");
        exit(1);
    }

    advance(parser, &scanner);

    consume(parser, &scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    stmt->indexed_columns = parse_indexed_column(parser, &scanner);

    consume(parser, &scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    fprintf(stderr, "\nIndex has columns: \n");
    for (size_t i = 0; i < stmt->indexed_columns->count; i++) {
        struct Column column =  stmt->indexed_columns->data[i];
        print_unterminated_string_to_stderr(&column.name);
    }
    fprintf(stderr, "\n");

    return stmt;
}






// @TODO: missing the following
// true
// false
// blob-literal
static struct NewExpr *parse_expression_new(struct Parser *parser, struct Scanner *scanner);
static struct TableExpression *parse_table_expression(struct Parser *parser, struct Scanner *scanner);
static struct SelectCore *parse_select_core(struct Parser *parser, struct Scanner *scanner);

static inline struct UnterminatedString unterminated_string_from_current_token(struct Parser *parser) {
    return (struct UnterminatedString){ .start = parser->current.start, .len = parser->current.length };
}

static bool match(struct Parser *parser, struct Scanner *scanner, enum TokenType token_type) {
    if (parser->current.type != token_type) {
        return false;
    }

    advance(parser, scanner);
    return true;
}

#define CASE_LITERAL                        \
        case TOKEN_NUMBER:                  \
        case TOKEN_STRING:                  \
        case TOKEN_NULL:                    \
        case TOKEN_CURRENT_TIME:            \
        case TOKEN_CURRENT_DATE:            \
        case TOKEN_CURRENT_TIMESTAMP:

static bool is_literal(struct Token *token) {
    // @TODO: missing some tokens
    switch (token->type) {
        CASE_LITERAL
            return true;
        
        default:
            return false;
    }
}

#define CASE_UNARY      \
    case TOKEN_PLUS:    \
    case TOKEN_MINUS:

static bool is_unary(struct Token *token) {
    switch (token->type) {
        CASE_UNARY
            return true;
        
        default:
            return false;
    }
}

static struct NewExprLiteral *new_expr_literal(struct ArenaAllocator *arena, enum LiteralType type, struct UnterminatedString text) {
    struct NewExprLiteral *literal = ARENA_ALLOC_TYPE(arena, struct NewExprLiteral);
    if (!literal) {
        fprintf(stderr, "new_expr_literal: failed to malloc *literal.\n");
        exit(1);
    }

    literal->type   = type;
    literal->text   = text;

    return literal;
}

static struct NewExprLiteral *make_literal_number(struct ArenaAllocator *arena, int64_t value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_NUMBER, text);
    literal->number.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_string(struct ArenaAllocator *arena, struct UnterminatedString value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_STRING, text);
    literal->string.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_null(struct ArenaAllocator *arena, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_NULL, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_time(struct ArenaAllocator *arena, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_CURRENT_TIME, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_date(struct ArenaAllocator *arena, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_CURRENT_DATE, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_timestamp(struct ArenaAllocator *arena, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_CURRENT_TIMESTAMP, text);
    return literal;
}

static struct NewExprLiteral *make_literal_boolean(struct ArenaAllocator *arena, bool value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_BOOLEAN, text);
    literal->boolean.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_blob(struct ArenaAllocator *arena, uint8_t *value, size_t length, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(arena, LITERAL_BLOB, text);
    literal->blob.value     = value;
    literal->blob.length    = length;
    return literal;
}

int64_t string_to_int(const char *str, size_t len) {
    int64_t ret = 0;
    for (size_t i = 0; i < len; i++) {
        ret = ret * 10 + (str[i] - '0');
    }
    return ret;
}

struct LiteralBlob hex_string_to_bytes(const char *str, size_t len) {
    assert(len % 2 == 0);

    uint8_t *bytes = malloc(len / 2);

    uint8_t current_byte, high, low;
    size_t str_idx = 0;
    size_t bytes_idx = 0;
    for (;;) {
        high = char_to_decimal[(unsigned char)str[str_idx++]];
        low  = char_to_decimal[(unsigned char)str[str_idx++]];

        if (high > 15 || low > 15) {
            fprintf(stderr, "Invalid hex characer %.*s\n", 2, &str[str_idx - 2]);
            exit(1);
        }

        current_byte = (high << 4) | low;

        bytes[bytes_idx++] = current_byte;

        if (str_idx == len) break;
    }

    return (struct LiteralBlob){ .value = bytes, .length = bytes_idx };
}

static struct NewExprLiteral *parse_literal(struct Parser *parser, struct Scanner *scanner) {
    struct UnterminatedString text = { .start = parser->current.start, .len = parser->current.length };
    struct NewExprLiteral *literal = NULL;
    switch (parser->current.type) {

        case TOKEN_NUMBER: {
            int64_t value = string_to_int(parser->current.start, parser->current.length);
            literal = make_literal_number(&parser->arena, value, text);
            break;
        }
            
        case TOKEN_STRING: {
            // Cut off start and end quotes
            struct UnterminatedString value = { .start = parser->current.start + 1, .len = parser->current.length - 2};
            literal = make_literal_string(&parser->arena, value, text);
            break;
        }

        case TOKEN_NULL:
            literal = make_literal_null(&parser->arena, text);
            break;

        case TOKEN_CURRENT_TIME:
            literal = make_literal_current_time(&parser->arena, text);
            break;

        case TOKEN_CURRENT_DATE:
            literal = make_literal_current_date(&parser->arena, text);
            break;

        case TOKEN_CURRENT_TIMESTAMP:
            literal = make_literal_current_timestamp(&parser->arena, text);
            break;

        case TOKEN_TRUE:
            literal = make_literal_boolean(&parser->arena, true, text);
            break;

        case TOKEN_FALSE:
            literal = make_literal_boolean(&parser->arena, false, text);
            break;

        case TOKEN_BLOB: {
            // Cut off starting X' and ending '
            struct LiteralBlob value = hex_string_to_bytes(parser->current.start + 2, parser->current.length - 3);       
            literal = make_literal_blob(&parser->arena, value.value, value.length, text);
            break;
        }

        default:
            fprintf(stderr, "parse_literal: unknown type %d\n", parser->current.type);
            error_at_current(parser, "Unknown type");
    }

    advance(parser, scanner);
    return literal;
}

#define CASE_TYPE_NAME              \
        case TOKEN_BLOB_KEYWORD:    \
        case TOKEN_INTEGER:         \
        case TOKEN_NULL:            \
        case TOKEN_REAL:            \
        case TOKEN_TEXT:

static enum CastType parse_type_name(struct Parser *parser, struct Scanner *scanner) {
    // @TODO: does not match sqlite type-name
    enum CastType type;

    switch (parser->current.type) {
        case TOKEN_BLOB_KEYWORD:
            type = TYPE_BLOB;
            break;

        case TOKEN_INTEGER:
            type = TYPE_INTEGER;
            break;

        case TOKEN_NULL:
            type = TYPE_NULL;
            break;

        case TOKEN_REAL:
            type = TYPE_REAL;
            break;

        case TOKEN_TEXT:
            type = TYPE_TEXT;
            break;

        default:
            fprintf(stderr, "parse_type_name: unknown type %d\n", parser->current.type);
            error_at_current(parser, "Unknown type");
    }

    advance(parser, scanner);
    return type;
}

static struct NewExprCast *parse_cast(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprCast temp = {0};

    consume(parser, scanner, TOKEN_CAST, "Expected 'CAST'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    temp.expr = parse_expression_new(parser, scanner);

    consume(parser, scanner, TOKEN_AS, "Expected 'AS'");
    temp.cast_type = parse_type_name(parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'");

    struct NewExprCast *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprCast);
    *node = temp;
    return node;
}

static struct NewExprCase *parse_case(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprCase temp = {
        .first_expr = NULL,
        .else_expr  = NULL
    };

    consume(parser, scanner, TOKEN_CASE, "Expected 'CASE'.");

    if (parser->current.type != TOKEN_WHEN) {
        temp.first_expr = parse_expression_new(parser, scanner);
    }

    temp.clauses = vector_case_clause_list_new();

    while (match(parser, scanner, TOKEN_WHEN)) {
        struct CaseClause clause;
        clause.when = parse_expression_new(parser, scanner);
        consume(parser, scanner, TOKEN_THEN, "Expected 'THEN'.");
        clause.then = parse_expression_new(parser, scanner);

        vector_case_clause_list_push(temp.clauses, clause);
    }

    if (match(parser, scanner, TOKEN_ELSE)) {
        temp.else_expr = parse_expression_new(parser, scanner);
    }

    consume(parser, scanner, TOKEN_END, "Expected 'END'.");

    struct NewExprCase *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprCase);
    *node = temp;
    return node;
}

static struct OrderByClause *parse_order_by_clause(struct Parser *parser, struct Scanner *scanner) {

    struct OrderByClause temp = {
        .sort_type        = SORT_ASC,
        .collate_type     = COLLATE_NONE,
        .nulls_position   = NULLS_NONE,
        .expr             = NULL
    };

    temp.expr = parse_expression_new(parser, scanner);

    if (match(parser, scanner, TOKEN_COLLATE)) {
        // @TODO: collation name to enum
    }

    if (match(parser, scanner, TOKEN_ASC)) {
        temp.sort_type = SORT_ASC;
    } else if (match(parser, scanner, TOKEN_DESC)) {
        temp.sort_type = SORT_DESC;
    }

    if (match(parser, scanner, TOKEN_NULLS)) {

        if (match(parser, scanner, TOKEN_FIRST)) {
            temp.nulls_position = NULLS_FIRST;
        } else if (match(parser, scanner, TOKEN_LAST)) {
            temp.nulls_position = NULLS_LAST;
        } else {
            error_at_current(parser, "Expected 'First' or 'Last'");
        }

    }

    struct OrderByClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct OrderByClause);
    *node = temp;
    return node;
}

static struct OrderByClausePtrList *collect_order_by_clauses(struct Parser *parser, struct Scanner *scanner) {
    struct OrderByClausePtrList *order_list = vector_order_by_clause_ptr_list_new();
    do {
        struct OrderByClause *order = parse_order_by_clause(parser, scanner);
        vector_order_by_clause_ptr_list_push(order_list, order);
    } while(match(parser, scanner, TOKEN_COMMA));

    return order_list;
}

static struct NewExprPtrList *collect_expressions(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprPtrList *expr_list = vector_new_expr_ptr_list_new();
    do {
        struct NewExpr *expr = parse_expression_new(parser, scanner);
        vector_new_expr_ptr_list_push(expr_list, expr);
    } while(match(parser, scanner, TOKEN_COMMA));

    return expr_list;
}

static struct NewExprFunctionArgs *parse_function_arguments(struct Parser *parser, struct Scanner *scanner) {

    struct NewExprFunctionArgs temp = {
        .distinct  = false,
        .star      = false,
        .order_by  = NULL,
        .exprs     = NULL
    };

    if (parser->current.type == TOKEN_RIGHT_PAREN) {
        // empty
    } else if (parser->current.type == TOKEN_STAR) {
        temp.star = true;
    } else {

        // Expr
        if (match(parser, scanner, TOKEN_DISTINCT)) {
            temp.distinct = true;
        }
        
        temp.exprs = collect_expressions(parser, scanner);
        
        if (match(parser, scanner, TOKEN_ORDER)) {
            consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
            temp.order_by = collect_order_by_clauses(parser, scanner);
        }
    }

    struct NewExprFunctionArgs *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprFunctionArgs);
    *node = temp;
    return node;
}

static struct FilterClause *parse_filter_clause(struct Parser *parser, struct Scanner *scanner) {
    struct FilterClause temp = {0};

    consume(parser, scanner, TOKEN_FILTER, "Expected 'FILTER'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    consume(parser, scanner, TOKEN_WHERE, "Expected 'WHERE'.");

    temp.expr = parse_expression_new(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    struct FilterClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct FilterClause);
    *node = temp;
    return node;
}

static struct FrameSpec *parse_frame_spec(struct Parser *parser, struct Scanner *scanner) {
    struct FrameSpec temp = {
        .start.offset   = NULL,
        .end            = { .type = FB_CURRENT_ROW, .offset = NULL },
        .exclude        = EXCLUDE_NO_OTHERS
    };

    switch (parser->current.type) {
        case TOKEN_RANGE:
            temp.type = FRAME_RANGE;
            break;

        case TOKEN_ROWS:
            temp.type = FRAME_ROWS;
            break;

        case TOKEN_GROUPS:
            temp.type = FRAME_GROUPS;
            break;

        default:
            error_at_current(parser, "Unknown FrameType.");
    }

    advance(parser, scanner);

    // @TODO: this is very messy, can we clean up?
    if (match(parser, scanner, TOKEN_BETWEEN)) {

        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                temp.start.type = FB_UNBOUNDED_PRECEDING;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Exptected 'ROW'.");
                temp.start.type = FB_CURRENT_ROW;
                break;

            default:
                temp.start.offset = parse_expression_new(parser, scanner);
                if (parser->current.type == TOKEN_PRECEDING) {
                    advance(parser, scanner);
                    temp.start.type = FB_N_PRECEDING;
                } else if (parser->current.type == TOKEN_FOLLOWING) {
                    advance(parser, scanner);
                    temp.start.type = FB_N_FOLLOWING;
                } else {
                    error_at_current(parser, "Expected 'PRECEDING' or 'FOLLOWING'.");
                }
        }

        consume(parser, scanner, TOKEN_AND, "Expected 'AND'.");

        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_FOLLOWING, "Expected 'FOLLOWING'.");
                temp.end.type = FB_UNBOUNDED_FOLLOWING;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Exptected 'ROW'.");
                temp.end.type = FB_CURRENT_ROW;
                break;

            default:
                temp.end.offset = parse_expression_new(parser, scanner);
                if (parser->current.type == TOKEN_PRECEDING) {
                    advance(parser, scanner);
                    temp.end.type = FB_N_PRECEDING;
                } else if (parser->current.type == TOKEN_FOLLOWING) {
                    advance(parser, scanner);
                    temp.end.type = FB_N_FOLLOWING;
                } else {
                    error_at_current(parser, "Expected 'PRECEDING' or 'FOLLOWING'.");
                }
        }

    } else {
        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                temp.start.type   = FB_UNBOUNDED_PRECEDING;
                temp.end.type     = FB_CURRENT_ROW;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Expected 'ROW'.");
                temp.start.type = FB_CURRENT_ROW;
                temp.end.type   = FB_CURRENT_ROW;
                break;

            default:
                temp.start.offset = parse_expression_new(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                temp.start.type = FB_N_PRECEDING;
                temp.end.type   = FB_CURRENT_ROW;
                break;
        }
    }

    // Exclude
    if (match(parser, scanner, TOKEN_EXCLUDE)) {
        
        switch (parser->current.type) {
            case TOKEN_NO:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_OTHERS, "Expected 'OTHERS'.");
                temp.exclude = EXCLUDE_NO_OTHERS;
                break;
            
            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Expected 'ROW'.");
                temp.exclude = EXCLUDE_CURRENT_ROW;
                break;
            
            case TOKEN_GROUP:
                advance(parser, scanner);
                temp.exclude = EXCLUDE_GROUP;
                break;
            
            case TOKEN_TIES:
                advance(parser, scanner);
                temp.exclude = EXCLUDE_TIES;
                break;
            
            default:
                error_at_current(parser, "Expected 'NO', 'CURRENT', 'GROUP' or 'TIES'.");
        }
    }

    struct FrameSpec *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct FrameSpec);
    *node = temp;
    return node;
}

static struct WindowDefinition *parse_window_definition(struct Parser *parser, struct Scanner *scanner) {

    struct WindowDefinition temp = {
        .base_window       = NULL,
        .partition_by      = NULL,
        .order_by          = NULL,
        .frame_spec        = NULL
    };

    // Check for base-window-name
    if (parser->current.type == TOKEN_IDENTIFIER) {
        temp.base_window = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);

        temp.base_window->start    = parser->current.start;
        temp.base_window->len      = parser->current.length;

        advance(parser, scanner);
    }

    if (match(parser, scanner, TOKEN_PARTITION)) {
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        temp.partition_by = collect_expressions(parser, scanner);
    }

    if (match(parser, scanner, TOKEN_ORDER)) {
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        temp.order_by = collect_order_by_clauses(parser, scanner);
    }

    if (parser->current.type == TOKEN_RANGE ||
        parser->current.type == TOKEN_ROWS ||
        parser->current.type == TOKEN_GROUPS)
        {
            temp.frame_spec = parse_frame_spec(parser, scanner);
        }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    struct WindowDefinition *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct WindowDefinition);
    *node = temp;
    return node;
}

static struct OverClause *parse_over_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_OVER, "Expected 'OVER'.");

    struct OverClause temp = {0};

    if (parser->current.type == TOKEN_IDENTIFIER) {
        temp.type = OVER_REFERENCED;
        
        temp.referenced_window = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        temp.referenced_window->start  = parser->current.start;
        temp.referenced_window->len    = parser->current.length;

        advance(parser, scanner);
    } else {

        consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
        
        temp.type          = OVER_INLINE;
        temp.inline_window = parse_window_definition(parser, scanner);
    }

    struct OverClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct OverClause);
    *node = temp;
    return node;
}

static struct NewExprFunction *new_expr_function() {
    struct NewExprFunction *function_expr = malloc(sizeof(struct NewExprFunction));
    if (!function_expr) {
        fprintf(stderr, "new_expr_function: failed to malloc *function_expr.\n");
        exit(1);
    }

    function_expr->filter   = NULL;
    function_expr->over     = NULL;

    return function_expr;
}

static struct NewExprFunction *parse_function(struct Parser *parser, struct Scanner *scanner) {
    if (parser->current.type != TOKEN_IDENTIFIER) {
        error_at_current(parser, "Expected function-name");
    }

    struct NewExprFunction temp = {
        .filter = NULL,
        .over   = NULL
    };

    temp.name = unterminated_string_from_current_token(parser);

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    temp.args = parse_function_arguments(parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    // Check for filter clause
    if (parser->current.type == TOKEN_FILTER) {
        temp.filter = parse_filter_clause(parser, scanner);
    }

    // Check for over clause
    if (parser->current.type == TOKEN_OVER) {
        temp.over = parse_over_clause(parser, scanner);
    }

    struct NewExprFunction *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprFunction);
    *node = temp;
    return node;
}

static struct QualifiedName *parse_name(struct Parser *parser, struct Scanner *scanner) {
    struct QualifiedName temp = {0};

    if (peek(parser, scanner, 3)->type == TOKEN_DOT) {
        // schema-name
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
        struct UnterminatedString schema_name = unterminated_string_from_current_token(parser);
        temp.parts[temp.count++] = schema_name;
        advance(parser, scanner);
    }
    
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        // table-name
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
        struct UnterminatedString table_name = unterminated_string_from_current_token(parser);
        temp.parts[temp.count++] = table_name;
        advance(parser, scanner);

    }

    // column-name
    if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
    struct UnterminatedString column_name = unterminated_string_from_current_token(parser);
    temp.parts[temp.count++] = column_name;
    advance(parser, scanner);

    struct QualifiedName *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct QualifiedName);
    *node = temp;
    return node;
}

static struct NewExprGrouping *parse_grouping(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprGrouping temp = {0};
    temp.inner = parse_expression_new(parser, scanner);
    struct NewExprGrouping *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprGrouping);
    *node = temp;
    return node;
}

static struct NewExprRaise *parse_raise_expr(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_RAISE, "Expected 'RAISE'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    struct NewExprRaise temp = {
        .expr = NULL
    };

    switch (parser->current.type) {

        case TOKEN_IGNORE:
            temp.type = RAISE_IGNORE;
            break;

        case TOKEN_ROLLBACK:
            consume(parser, scanner, TOKEN_COMMA, "Expected ','.");
            temp.type = RAISE_ROLLBACK;
            temp.expr = parse_expression_new(parser, scanner);
            break;

        case TOKEN_ABORT:
            temp.type = RAISE_ABORT;
            break;

        case TOKEN_FAIL:
            temp.type = RAISE_FAIL;
            break;

        default:
            error_at_current(parser, "Expected 'IGNORE', 'ROLLBACK', 'ABORT' or 'FAIL'.");
    }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    struct NewExprRaise *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprRaise);
    *node = temp;
    return node;
}

static struct ResultColumn *parse_result_column(struct Parser *parser, struct Scanner *scanner) {
    struct ResultColumn temp = {0};

    if (match(parser, scanner, TOKEN_STAR)) {
        temp.type = RC_ALL;
    } 
    
    else if (parser->current.type == TOKEN_IDENTIFIER && peek(parser, scanner, 1)->type == TOKEN_DOT) {
        temp.type                         = RC_TABLE_ALL;
        temp.table_all.table_name         = unterminated_string_from_current_token(parser);
        
        advance(parser, scanner);
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_STAR, "Exptected '*'.");
    }

    else {
        temp.type         = RC_EXPR;
        temp.expr.expr    = parse_expression_new(parser, scanner);
        temp.expr.alias   = NULL;
        
        if (match(parser, scanner, TOKEN_AS)) {
            if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Exptected identifier.");
            
            temp.expr.alias->start    = parser->current.start;
            temp.expr.alias->len      = parser->current.length;
            
            advance(parser, scanner);
        }
    }

    struct ResultColumn *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct ResultColumn);
    *node = temp;
    return node;
}

static struct ResultColumnPtrList *collect_result_columns(struct Parser *parser, struct Scanner *scanner) {
    struct ResultColumnPtrList *result_column_ptr_list = vector_result_column_ptr_list_new();
    do {
        struct ResultColumn *result_column = parse_result_column(parser, scanner);
        vector_result_column_ptr_list_push(result_column_ptr_list, result_column);
    } while (match(parser, scanner, TOKEN_COMMA));

    return result_column_ptr_list;
}

static void tos_parse_alias(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    match(parser, scanner, TOKEN_AS); // Consume 'AS' if it exists
    
    if (parser->current.type == TOKEN_IDENTIFIER) {
        // @TODO: are all following identifiers aliases?
        tos->alias        = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        tos->alias->start = parser->current.start;
        tos->alias->len   = parser->current.length;
        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }
}

static void tos_parse_table_name(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    tos->type                       = TOS_TABLE_NAME;
    struct TableName temp = {0};

    // schema-name
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        advance(parser, scanner);
        
        temp.schema_name         = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        temp.schema_name->start  = parser->current.start;
        temp.schema_name->len    = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }

    temp.table_name = unterminated_string_from_current_token(parser);

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    tos_parse_alias(parser, scanner, tos);

    // Check for index
    if (match(parser, scanner, TOKEN_INDEXED)) {
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");

        temp.index_mode           = TABLE_INDEX_NAMED;
        temp.index_name           = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        temp.index_name->start    = parser->current.start;
        temp.index_name->len      = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    } else if (match(parser, scanner, TOKEN_NOT)) {
        consume(parser, scanner, TOKEN_INDEXED, "Expected 'INDEXED'.");

        temp.index_mode           = TABLE_INDEX_NONE;
        temp.index_name           = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        temp.index_name->start    = parser->current.start;
        temp.index_name->len      = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    } else {
        temp.index_mode           = TABLE_INDEX_AUTO;
    }

    struct TableName *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct TableName);
    *node = temp;
    tos->table_name = node;
}

static void tos_parse_table_function(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    tos->type                               = TOS_TABLE_FUNCTION;

    struct TableFunction temp = {0};

    // schema-name
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        advance(parser, scanner);
        
        temp.schema_name         = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct UnterminatedString);
        temp.schema_name->start  = parser->current.start;
        temp.schema_name->len    = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }
    
    temp.function_name = unterminated_string_from_current_token(parser);

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    temp.args = collect_expressions(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    tos_parse_alias(parser, scanner, tos);
    
    struct TableFunction *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct TableFunction);
    *node = temp;
    tos->table_function = node;
}

#define CASE_COMPOUND_OPERATOR      \
        case TOKEN_UNION:           \
        case TOKEN_INTERSECT:       \
        case TOKEN_EXCEPT:

static bool is_compound_operator(struct Parser *parser) {
    switch (parser->current.type) {
        CASE_COMPOUND_OPERATOR
            return true;

        default:
            return false;
    }
}

static struct LimitClause *parse_limit_clause(struct Parser *parser, struct Scanner *scanner) {
    struct LimitClause temp = {
        .offset = NULL
    };

    struct NewExpr *expr = parse_expression_new(parser, scanner);

    if (match(parser, scanner, TOKEN_COMMA)) {
        temp.offset = expr;
        temp.limit  = parse_expression_new(parser, scanner);
    } else if (match(parser, scanner, TOKEN_OFFSET)) {
        temp.offset = parse_expression_new(parser, scanner);
        temp.limit  = expr;
    } else {
        temp.limit  = expr;
    }

    struct LimitClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct LimitClause);
    *node = temp;
    return node;
}

struct SelectStatementNew *parse_select_statement_new(struct Parser *parser, struct Scanner *scanner) {
    struct SelectStatementNew temp = {
        .order = NULL,
        .limit = NULL
    };

    if (parser->current.type == TOKEN_WITH) {
        // @TODO: implement
    }

    temp.cores = vector_select_core_data_ptr_list_new();
    
    struct SelectCoreData core_temp = {
        .type = COMPOUND_OPERATOR_BASE,
        .core = parse_select_core(parser, scanner)
    };

    struct SelectCoreData *core_node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct SelectCoreData);
    *core_node = core_temp;

    vector_select_core_data_ptr_list_push(temp.cores, core_node);

    while (is_compound_operator(parser)) {
        enum CompoundOperatorType compound_type;

        switch(parser->current.type) {
            case TOKEN_UNION:
                if (peek(parser, scanner, 1)->type == TOKEN_ALL) {
                    advance(parser, scanner); // Consume 'UNION' so that next advance consumes 'ALL'
                    compound_type = COMPOUND_OPERATOR_UNION_ALL;
                } else {
                    compound_type = COMPOUND_OPERATOR_UNION;
                }
                break;

            case TOKEN_INTERSECT:
                compound_type = COMPOUND_OPERATOR_INTERSECT;
                break;

            case TOKEN_EXCEPT:
                compound_type = COMPOUND_OPERATOR_EXCEPT;
                break;

            default:
                error_at_current(parser, "Expected 'UNION', 'INTERSECT' or 'EXCEPT'.");
        }

        advance(parser, scanner);

        core_temp.type = compound_type;
        core_temp.core = parse_select_core(parser, scanner);

        core_node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct SelectCoreData);
        *core_node = core_temp;

        vector_select_core_data_ptr_list_push(temp.cores, core_node);
    }

    if (match(parser, scanner, TOKEN_ORDER)) {
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        
        temp.order = collect_order_by_clauses(parser, scanner);
    }

    if (match(parser, scanner, TOKEN_LIMIT)) {
        temp.limit = parse_limit_clause(parser, scanner);
    }

    struct SelectStatementNew *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct SelectStatementNew);
    *node = temp;
    return node;
}

static bool is_start_of_select(struct Parser *parser) {
    return  parser->current.type == TOKEN_WITH
            || parser->current.type == TOKEN_SELECT
            || parser->current.type == TOKEN_VALUES;
}

static struct TableOrSubquery *parse_table_or_subquery(struct Parser *parser, struct Scanner *scanner) {
    struct TableOrSubquery temp = {
        .alias = NULL
    };

    if (is_start_of_select(parser)) {
        temp.type       = TOS_SUBQUERY;
        temp.subquery   = parse_select_statement_new(parser, scanner);
        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        tos_parse_alias(parser, scanner, &temp);
    }

    // table-function-name(
    else if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
        temp.type           = TOS_TABLE_FUNCTION;
        tos_parse_table_function(parser, scanner, &temp);
    }

    // schema-name.table-function-name(
    else if (peek(parser, scanner, 1)->type == TOKEN_DOT &&
        peek(parser, scanner, 3)->type == TOKEN_LEFT_PAREN) {

        temp.type           = TOS_TABLE_FUNCTION;
        tos_parse_table_function(parser, scanner, &temp);
    }

    else {   
        temp.type       = TOS_TABLE_NAME;
        tos_parse_table_name(parser, scanner, &temp);
    }

    struct TableOrSubquery *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct TableOrSubquery);
    *node = temp;
    return node;
}

static bool is_start_of_join_operator(struct Parser *parser) {
    switch (parser->current.type) {

        case TOKEN_COMMA:
        case TOKEN_JOIN:
        case TOKEN_NATURAL:
        case TOKEN_LEFT:
        case TOKEN_RIGHT:
        case TOKEN_FULL:
        case TOKEN_INNER:
        case TOKEN_CROSS:
            return true;

        default:
            return false;

    }
}

static void join_data_parse_join_operator(struct Parser *parser, struct Scanner *scanner, struct JoinData *join_data) {
    if (parser->current.type == TOKEN_NATURAL) {
        join_data->natural = true;
        advance(parser, scanner);
    }

    switch (parser->current.type) {
        case TOKEN_CROSS:
        case TOKEN_COMMA:
            join_data->join_operator = JO_CROSS;
            break;

        case TOKEN_LEFT:
            join_data->join_operator = JO_LEFT_OUTER;
            break;

        case TOKEN_RIGHT:
            join_data->join_operator = JO_RIGHT_OUTER;
            break;

        case TOKEN_FULL:
            join_data->join_operator = JO_FULL_OUTER;
            break;

        case TOKEN_INNER:
            join_data->join_operator = JO_INNER;
            break;

        default:
            error_at_current(parser, "Unknown join operator.");
    }

    if (parser->current.type == TOKEN_OUTER) {
        advance(parser, scanner);
    }

    consume(parser, scanner, TOKEN_JOIN, "Expected 'JOIN'.");
}

static struct UnterminatedStringList *collect_unterminated_strings(struct Parser *parser, struct Scanner *scanner) {
    struct UnterminatedStringList *list = vector_unterminated_string_list_new();

    do {
        struct UnterminatedString string = { .start = parser->current.start, .len = parser->current.length };
        vector_unterminated_string_list_push(list, string);
        advance(parser, scanner);
    } while (match(parser, scanner, TOKEN_COMMA));

    return list;
}

static struct JoinConstraint *parse_join_constraint(struct Parser *parser, struct Scanner *scanner) {
    struct JoinConstraint *join_constraint = NULL;
    
    switch (parser->current.type) {
        case TOKEN_ON:
            advance(parser, scanner);
            join_constraint = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct JoinConstraint);
            join_constraint->type = JOIN_CONSTRAINT_ON;
            join_constraint->expr = parse_expression_new(parser, scanner);
            break;

        case TOKEN_USING:
            advance(parser, scanner);
            consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
            join_constraint = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct JoinConstraint);
            join_constraint->type           = JOIN_CONSTRAINT_USING;
            join_constraint->column_names   = collect_unterminated_strings(parser, scanner);
            consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
            break;

        default:
            error_at_current(parser, "Expected 'ON' or 'USING'.");
    }

    return join_constraint;
}

static struct JoinClause *parse_join_clause(struct Parser *parser, struct Scanner *scanner) {
    struct JoinClause temp = {0};

    temp.left   = parse_table_expression(parser, scanner);
    temp.joins  = vector_join_data_ptr_list_new();

    while (is_start_of_join_operator(parser)) {
        struct JoinData join_data = {
            .natural      = false,
            .right        = NULL,
            .constraint   = NULL
        };

        // parse join-operator
        join_data_parse_join_operator(parser, scanner, &join_data);

        // parse table-or-subquery
        join_data.right = parse_table_expression(parser, scanner);

        // parse join-constraint
        join_data.constraint = parse_join_constraint(parser, scanner);

        if (join_data.natural && join_data.constraint != NULL) {
            fprintf(stderr, "join_data->constraint should be NULL if join_data->natural == true.\n");
            exit(1);
        }

        struct JoinData *join_data_alloc = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct JoinData);
        *join_data_alloc = join_data;
        vector_join_data_ptr_list_push(temp.joins, join_data_alloc);
    }

    struct JoinClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct JoinClause);
    *node = temp;
    return node;
}

static struct TableExpression *parse_table_expression(struct Parser *parser, struct Scanner *scanner) {
    struct TableExpression temp = {0};

    if (parser->current.type == TOKEN_IDENTIFIER) {
        temp.type     = TE_SIMPLE;
        temp.simple   = parse_table_or_subquery(parser, scanner);
        goto finish;
    } 
    
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    
    if (is_start_of_select(parser)) {
        temp.type     = TE_SIMPLE;
        temp.simple   = parse_table_or_subquery(parser, scanner);
        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        goto finish;
    } 
    
    // Join clause
    // Either explicit or comma separated list for cross joins
    temp.type = TE_JOIN;
    temp.join = parse_join_clause(parser, scanner);

    finish:
    struct TableExpression *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct TableExpression);
    *node = temp;
    return node; 
}

static struct FromClause *parse_from_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_FROM, "Expected 'FROM'.");
    struct FromClause temp = {0};
    // Join clause can have a standalone table
    temp.tables = parse_join_clause(parser, scanner);

    struct FromClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct FromClause);
    *node = temp;
    return node;
}

static struct WhereClause *parse_where_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_WHERE, "Expected 'WHERE'.");
    struct WhereClause temp = {0};
    temp.expr = parse_expression_new(parser, scanner);

    struct WhereClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct WhereClause);
    *node = temp;
    return node;
}

static struct GroupByClause *parse_group_by(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_GROUP, "Expected 'GROUP'.");
    consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
    struct GroupByClause temp = {0};
    temp.expr_ptr_list = collect_expressions(parser, scanner);

    struct GroupByClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct GroupByClause);
    *node = temp;
    return node;
}

static struct HavingClause *parse_having_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_HAVING, "Expected 'HAVING'");
    struct HavingClause temp = {0};
    temp.expr = parse_expression_new(parser, scanner);

    struct HavingClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct HavingClause);
    *node = temp;
    return node;
}

static struct WindowData *parse_window_data(struct Parser *parser, struct Scanner *scanner) {
    struct WindowData temp = {0};

    temp.name = unterminated_string_from_current_token(parser);

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    consume(parser, scanner, TOKEN_AS, "Expected 'AS'.");

    temp.definition = parse_window_definition(parser, scanner);

    struct WindowData *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct WindowData);
    *node = temp;
    return node;
}

static struct WindowDataPtrList *collect_window_data(struct Parser *parser, struct Scanner *scanner) {
    struct WindowDataPtrList *window_data_ptr_list = vector_window_data_ptr_list_new();

    do {
        struct WindowData *window_data = parse_window_data(parser, scanner);
        vector_window_data_ptr_list_push(window_data_ptr_list, window_data);
    } while (match(parser, scanner, TOKEN_COMMA));

    return window_data_ptr_list;
}

static struct WindowClause *parse_window_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_WINDOW, "Expected 'WINDOW'.");
    struct WindowClause temp = {0};

    temp.window_list = collect_window_data(parser, scanner);

    struct WindowClause *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct WindowClause);
    *node = temp;
    return node;
}

static struct NewExprPtrListPtrList *collect_values(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    struct NewExprPtrListPtrList *list_of_lists = vector_new_expr_ptr_list_ptr_list_new();

    do {
        consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

        struct NewExprPtrList *list = collect_expressions(parser, scanner);
        vector_new_expr_ptr_list_ptr_list_push(list_of_lists, list);

        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
    } while(match(parser, scanner, TOKEN_COMMA));

    return list_of_lists;
}

static struct NewExprRowValue *new_row_value_expr() {
    struct NewExprRowValue *row_value_epxr = malloc(sizeof(struct NewExprRowValue));
    if (!row_value_epxr) {
        fprintf(stderr, "new_row_value_expr: failed to malloc *row_value_expr.\n");
        exit(1);
    }

    return row_value_epxr;
}

static struct NewExprRowValue *parse_row_value_expr(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprRowValue temp = {0};

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    temp.elements = collect_expressions(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    struct NewExprRowValue *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprRowValue);
    *node = temp;
    return node;
}

static struct SelectCore *parse_select_core(struct Parser *parser, struct Scanner *scanner) {
    struct SelectCore temp = {0};

    if (match(parser, scanner, TOKEN_VALUES)) {
        consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

        temp.type           = SC_VALUES;
        temp.values.values  = collect_values(parser, scanner);

        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        goto finish;
    }
    
    consume(parser, scanner, TOKEN_SELECT, "Expected 'SELECT'.");
    temp.type = SC_SELECT;

    if (match(parser, scanner, TOKEN_DISTINCT)) {
        temp.select.distinct = true;
    } else if (match(parser, scanner, TOKEN_ALL)) {
        // Consume ALL
    }

    temp.select.result_columns = collect_result_columns(parser, scanner);

    if (parser->current.type == TOKEN_FROM) {
        temp.select.from = parse_from_clause(parser, scanner);
    }

    if (parser->current.type == TOKEN_WHERE) {
        temp.select.where = parse_where_clause(parser, scanner);
    }

    if (parser->current.type == TOKEN_GROUP) {
        temp.select.group_by = parse_group_by(parser, scanner);
        
        if (parser->current.type == TOKEN_HAVING) {
            temp.select.having = parse_having_clause(parser, scanner);
        }
    }

    if (parser->current.type == TOKEN_WINDOW) {
        temp.select.window = parse_window_clause(parser, scanner);
    }

    finish:
    struct SelectCore *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct SelectCore);
    *node = temp;

    return node;
}

static struct NewExpr *parse_primary_expression(struct Parser *parser, struct Scanner *scanner) {
    /* 
    Expect:
        Literal
        Bind parameter
        Schema-name or table-name or column-name
        Function-name -> (
        ( -> expr or ( -> select-stmt (TOKEN_WITH, TOKEN_SELECT, TOKEN_VALUES)
        Cast
        NOT -> EXISTS
        EXISTS
        Case
        Raise

    Todo:
        bind parameter
    */

    struct NewExpr temp = {0};

    struct UnterminatedString text = { .start = parser->current.start, .len = 0 }; // Update length later

    switch (parser->current.type) {
        CASE_LITERAL
            temp.type      = EXPR_LITERAL;
            temp.literal   = parse_literal(parser, scanner);
            break;
        
        case TOKEN_CAST:
            temp.type      = EXPR_CAST;
            temp.cast      = parse_cast(parser, scanner);
            break;

        case TOKEN_CASE:
            temp.type      = EXPR_CASE;
            temp.expr_case = parse_case(parser, scanner);
            break;

        case TOKEN_IDENTIFIER:
            if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
                // Function
                temp.type      = EXPR_FUNC;
                temp.function  = parse_function(parser, scanner);
            } else {
                // schema-name or table-name or column-name
                temp.type = EXPR_NAME;
                temp.name = parse_name(parser, scanner);
            }

            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            advance(parser, scanner);
            if (is_start_of_select(parser))
                {
                    temp.type      = EXPR_SUBQUERY;
                    temp.subquery  = parse_select_statement_new(parser, scanner);
                // @TODO
                // select-stmt
                }
            else if (peek(parser, scanner, 1)->type == TOKEN_COMMA){
                // row value expr
                temp.type      = EXPR_ROW_VALUE;
                temp.row_value = parse_row_value_expr(parser, scanner);
            } else {
                // single expr
                temp.type      = EXPR_GROUPING;
                temp.grouping  = parse_grouping(parser, scanner);
            }
            consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
            break;

        case TOKEN_NOT:
            // Fall through if peek(1) not 'EXISTS'
            if (peek(parser, scanner, 1)->type == TOKEN_EXISTS) {
                break;
            }

        case TOKEN_EXISTS:
            break;

        case TOKEN_RAISE:
            temp.type  = EXPR_RAISE;
            temp.raise = parse_raise_expr(parser, scanner);
            break;

        default:
            error_at_current(parser, "Expected Primary Expression");
    }

    text.len = parser->previous.start - text.start + parser->previous.length;
    temp.text = text;

    struct NewExpr *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExpr);
    *node = temp;
    return node;
}

#define CASE_PATTERN_MATCH             \
        case TOKEN_LIKE:               \
        case TOKEN_GLOB:               \
        case TOKEN_REGEXP:             \
        case TOKEN_MATCH:

#define CASE_BINARY     \
        case TOKEN_PLUS:

#define CASE_NULL_COMP      \
        case TOKEN_ISNULL:  \
        case TOKEN_NOTNULL: \
        case TOKEN_NULL:


static struct NewExprCollate *parse_collate(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    consume(parser, scanner, TOKEN_COLLATE, "Expected 'COLLATE'.");

    struct NewExprCollate temp = {0};

    temp.expr = primary_expr;
    temp.collation_name = unterminated_string_from_current_token(parser);
    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    struct NewExprCollate *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprCollate);
    *node = temp;
    return node;
}

static struct NewExprPatternMatch *parse_pattern_match(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr, bool not) {
    struct NewExprPatternMatch temp = {
        .escape = NULL,
        .not    = not,
        .left   = primary_expr
    };

    switch (parser->current.type) {

        case TOKEN_GLOB:
            temp.type     = PATTERN_GLOB;
            temp.right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_REGEXP:
            temp.type     = PATTERN_REGEXP;
            temp.right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_MATCH:
            temp.type     = PATTERN_MATCH;
            temp.right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_LIKE:
            temp.type     = PATTERN_LIKE;
            temp.right    = parse_expression_new(parser, scanner);
            if (match(parser, scanner, TOKEN_ESCAPE)) {
                temp.escape = parse_expression_new(parser, scanner);
            }
            break;

        default:
            error_at_current(parser, "Expected 'GLOB', 'REGEXP', 'MATCH' or 'LIKE'.");
    }

    struct NewExprPatternMatch *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprPatternMatch);
    *node = temp;
    return node;
}

static struct NewExprBinary *parse_binary_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprBinary temp = {0};

    temp.left = primary_expr;

    switch (parser->current.type) {
        case TOKEN_EQUAL:
            temp.op = BIN_EQUAL;
            break;

        case TOKEN_GREATER:
            temp.op = BIN_GREATER;
            break;

        case TOKEN_LESS:
            temp.op = BIN_LESS;
            break;

        default:
            error_at_current(parser, "Expected binary operator");
    }

    temp.right = parse_expression_new(parser, scanner);

    struct NewExprBinary *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprBinary);
    *node = temp;
    return node;
}

static struct NewExprNullComp *parse_null_comp(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprNullComp temp = {0};

    temp.expr = primary_expr;

    switch (parser->current.type) {
        case TOKEN_ISNULL:
            temp.type = NULL_COMP_ISNULL;
            break;

        case TOKEN_NOTNULL:
            temp.type = NULL_COMP_NOTNULL;
            break;

        case TOKEN_NULL: // Not has already been consumed
            temp.type = NULL_COMP_NOTNULL;
            break;

        default:
            error_at_current(parser, "Expected 'ISNULL', 'NOTNULL' or 'NOT'.");
    }

    advance(parser, scanner);

    struct NewExprNullComp *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprNullComp);
    *node = temp;
    return node;
}

static struct NewExprIs *parse_is_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprIs temp = {
        .not        = false,
        .distinct   = false
    };

    consume(parser, scanner, TOKEN_IS, "Expected 'IS'.");

    if (match(parser, scanner, TOKEN_NOT)) {
        temp.not = true;
    }

    if (match(parser, scanner, TOKEN_DISTINCT)) {
        temp.distinct = true;
        consume(parser, scanner, TOKEN_FROM, "Expected 'FROM'.");
    }

    temp.left   = primary_expr;
    temp.right  = parse_expression_new(parser, scanner);

    struct NewExprIs *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprIs);
    *node = temp;
    return node;
}

static struct NewExprBetween *parse_between_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr, bool not) {
    struct NewExprBetween temp = {
        .not        = not,
        .primary    = primary_expr
    };

    consume(parser, scanner, TOKEN_BETWEEN, "Expected 'BETWEEN'.");

    temp.left = parse_expression_new(parser, scanner);
    consume(parser, scanner, TOKEN_AND, "Expected 'AND'.");
    temp.right = parse_expression_new(parser, scanner);

    struct NewExprBetween *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExprBetween);
    *node = temp;
    return node;
}

static void parse_secondary_expression(struct Parser *parser, struct Scanner *scanner, struct NewExpr *parent_expr) {
    struct NewExpr *primary_expr = parse_primary_expression(parser, scanner);
    
    bool not = false;
    if (parser->current.type == TOKEN_NOT) {
        advance(parser, scanner);
        not = true;
    }

    switch (parser->current.type) {
        
        case TOKEN_COLLATE:
            parent_expr->type       = NEW_EXPR_COLLATE;
            parent_expr->collate    = parse_collate(parser, scanner, primary_expr);
            break;

        CASE_PATTERN_MATCH
            parent_expr->type           = NEW_EXPR_PATTERN_MATCH;
            parent_expr->pattern_match  = parse_pattern_match(parser, scanner, primary_expr, not);
            break;

        CASE_BINARY
            parent_expr->type           = NEW_EXPR_BINARY;
            parent_expr->binary         = parse_binary_expr(parser, scanner, primary_expr);
            break;

        CASE_NULL_COMP
            parent_expr->type           = NEW_EXPR_NULL_COMP;
            parent_expr->null_comp      = parse_null_comp(parser, scanner, primary_expr);
            break;

        case TOKEN_IS:
            parent_expr->type           = NEW_EXPR_IS;
            parent_expr->is             = parse_is_expr(parser, scanner, primary_expr);
            break;

        case TOKEN_BETWEEN:
            parent_expr->type           = NEW_EXPR_BETWEEN;
            parent_expr->between        = parse_between_expr(parser, scanner, primary_expr, not);
            break;

        // @TODO: need to implement
        case TOKEN_IN:
            break;
            
        default:
            error_at_current(parser, "Could not understand secondary expression.");

    }
}

static struct NewExpr *parse_expression_new(struct Parser *parser, struct Scanner *scanner) {
    /* 
    Expect:
        Literal
        Bind parameter
        Schema-name or table-name or column-name
        Function-name -> (
        ( -> expr or ( -> select-stmt (TOKEN_WITH, TOKEN_SELECT, TOKEN_VALUES)
        Cast
        NOT -> EXISTS
        EXISTS
        Case
        Raise

    Todo:
        bind parameter
    */

    struct NewExpr temp = {0};
    struct UnterminatedString text = { .start = parser->current.start, .len = 0 }; // Update length later

    switch (parser->current.type) {
        CASE_LITERAL
            temp.type      = EXPR_LITERAL;
            temp.literal   = parse_literal(parser, scanner);
            break;
        
        case TOKEN_CAST:
            temp.type      = EXPR_CAST;
            temp.cast      = parse_cast(parser, scanner);
            break;

        case TOKEN_CASE:
            temp.type      = EXPR_CASE;
            temp.expr_case = parse_case(parser, scanner);
            break;

        case TOKEN_IDENTIFIER:
            if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
                // Function
                temp.type      = EXPR_FUNC;
                temp.function  = parse_function(parser, scanner);
            } else {
                // schema-name or table-name or column-name
                temp.type = EXPR_NAME;
                temp.name = parse_name(parser, scanner);
            }

            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            advance(parser, scanner);
            if (is_start_of_select(parser))
                {
                    temp.type      = EXPR_SUBQUERY;
                    temp.subquery  = parse_select_statement_new(parser, scanner);
                // @TODO
                // select-stmt
                }
            else if (peek(parser, scanner, 1)->type == TOKEN_COMMA){
                // row value expr
                temp.type = EXPR_ROW_VALUE;
                temp.row_value = parse_row_value_expr(parser, scanner);
            } else {
                // single expr
                temp.type      = EXPR_GROUPING;
                temp.grouping  = parse_grouping(parser, scanner);
            }
            consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
            break;

        case TOKEN_NOT:
            // Fall through if peek(1) not 'EXISTS'
            if (peek(parser, scanner, 1)->type == TOKEN_EXISTS) {
                break;
            }

        case TOKEN_EXISTS:
            break;

        case TOKEN_RAISE:
            temp.type  = EXPR_RAISE;
            temp.raise = parse_raise_expr(parser, scanner);
            break;

        default:
            // Must start with expr
            parse_secondary_expression(parser, scanner, &temp);
            break;
    }

    text.len = parser->previous.start - text.start + parser->previous.length;
    temp.text = text;

    struct NewExpr *node = ARENA_ALLOC_TYPE_CHECKED(&parser->arena, struct NewExpr);
    *node = temp;
    return node;
}

struct SelectStatementNew *parse_new(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool) {

    struct Scanner scanner;

    init_scanner(&scanner, source, reserved_words_pool);

    advance(parser, &scanner);
    struct SelectStatementNew *select_stmt = parse_select_statement_new(parser, &scanner);

    return select_stmt;
}
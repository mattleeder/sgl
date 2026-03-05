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

static struct NewExprLiteral *new_expr_literal(enum LiteralType type, struct UnterminatedString text) {
    struct NewExprLiteral *literal = malloc(sizeof(struct NewExprLiteral));
    if (!literal) {
        fprintf(stderr, "new_expr_literal: failed to malloc *literal.\n");
        exit(1);
    }

    literal->type   = type;
    literal->text   = text;

    return literal;
}

static struct NewExprLiteral *make_literal_number(int64_t value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_NUMBER, text);
    literal->number.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_string(struct UnterminatedString value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_STRING, text);
    literal->string.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_null(struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_NULL, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_time(struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_CURRENT_TIME, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_date(struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_CURRENT_DATE, text);
    return literal;
}

static struct NewExprLiteral *make_literal_current_timestamp(struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_CURRENT_TIMESTAMP, text);
    return literal;
}

static struct NewExprLiteral *make_literal_boolean(bool value, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_BOOLEAN, text);
    literal->boolean.value = value;
    return literal;
}

static struct NewExprLiteral *make_literal_blob(uint8_t *value, size_t length, struct UnterminatedString text) {
    struct NewExprLiteral *literal = new_expr_literal(LITERAL_BLOB, text);
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
            fprintf(stderr, "Invalid hex characer %.*s\n", 2, str[str_idx - 2]);
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
            literal = make_literal_number(value, text);
            break;
        }
            
        case TOKEN_STRING: {
            // Cut off start and end quotes
            struct UnterminatedString value = { .start = parser->current.start + 1, .len = parser->current.length - 2};
            literal = make_literal_string(value, text);
            break;
        }

        case TOKEN_NULL:
            literal = make_literal_null(text);
            break;

        case TOKEN_CURRENT_TIME:
            literal = make_literal_current_time(text);
            break;

        case TOKEN_CURRENT_DATE:
            literal = make_literal_current_date(text);
            break;

        case TOKEN_CURRENT_TIMESTAMP:
            literal = make_literal_current_timestamp(text);
            break;

        case TOKEN_TRUE:
            literal = make_literal_boolean(true, text);
            break;

        case TOKEN_FALSE:
            literal = make_literal_boolean(false, text);
            break;

        case TOKEN_BLOB: {
            // Cut off starting X' and ending '
            struct LiteralBlob value = hex_string_to_bytes(parser->current.start + 2, parser->current.length - 3);       
            literal = make_literal_blob(value.value, value.length, text);
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

static struct NewExprCast *new_cast_expr() {
    struct NewExprCast *cast_expr = malloc(sizeof(struct NewExprCast));
    if (!cast_expr) {
        fprintf(stderr, "new_cast_expr: Failed to malloc *cast_expr.\n");
        exit(1);
    }

    return cast_expr;
}

static struct NewExprCast *parse_cast(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_CAST, "Expected 'CAST'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    struct NewExpr *expr = parse_expression_new(parser, scanner);

    consume(parser, scanner, TOKEN_AS, "Expected 'AS'");
    enum CastType cast_type = parse_type_name(parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'");

    struct NewExprCast *cast_expr = new_cast_expr();

    cast_expr->expr         = expr;
    cast_expr->cast_type    = cast_type;

    return cast_expr;
}

static struct NewExprCase *new_expr_case() {
    struct NewExprCase *expr_case = malloc(sizeof(struct NewExprCase));
    if (!expr_case) {
        fprintf(stderr, "new_expr_case: failed to malloc *case.\n");
        exit(1);
    }

    return expr_case;
}

static struct NewExprCase *parse_case(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_CASE, "Expected 'CASE'.");
    struct NewExpr *first_expr = NULL;
    struct NewExpr *else_expr  = NULL;

    if (parser->current.type != TOKEN_WHEN) {
        first_expr = parse_expression_new(parser, scanner);
    }

    struct CaseClauseList *case_clause_list = vector_case_clause_list_new();

    while (parser->current.type == TOKEN_WHEN) {
        struct CaseClause clause;
        consume(parser, scanner, TOKEN_WHEN, "Expected 'WHEN'.");
        clause.when = parse_expression_new(parser, scanner);
        consume(parser, scanner, TOKEN_THEN, "Expected 'THEN'.");
        clause.then = parse_expression_new(parser, scanner);

        vector_case_clause_list_push(case_clause_list, clause);
    }

    if (parser->current.type == TOKEN_ELSE) {
        consume(parser, scanner, TOKEN_ELSE, "Expected 'ELSE'.");
        else_expr = parse_expression_new(parser, scanner);
    }

    consume(parser, scanner, TOKEN_END, "Expected 'END'.");

    struct NewExprCase *case_expr = new_expr_case();

    case_expr->first_expr   = first_expr;
    case_expr->clauses      = case_clause_list;
    case_expr->else_expr    = else_expr;

    return case_expr;
}

static struct OrderByClause *new_order_by_clause() {
    struct OrderByClause *clause = malloc(sizeof(struct OrderByClause));
    if (!clause) {
        fprintf(stderr, "new_order_by_clause: failed to malloc *clause.\n");
        exit(1);
    }

    clause->sort_type       = SORT_NONE;
    clause->collate_type    = COLLATE_NONE;
    clause->nulls_position  = NULLS_NONE;
    clause->expr            = NULL;

    return clause;
}

static struct OrderByClause *parse_order_by_clause(struct Parser *parser, struct Scanner *scanner) {
    struct OrderByClause *clause = new_order_by_clause();

    clause->expr = parse_expression_new(parser, scanner);

    if (parser->current.type == TOKEN_COLLATE) {
        advance(parser, scanner);
        // @TODO: collation name to enum
    }

    if (parser->current.type == TOKEN_ASC) {
        clause->sort_type = SORT_ASC;
    } else if (parser->current.type == TOKEN_DESC) {
        clause->sort_type = SORT_DESC;
    }

    if (parser->current.type != TOKEN_NULLS) {
        return clause;
    }

    advance(parser, scanner);

    if (parser->current.type == TOKEN_FIRST) {
        clause->nulls_position = NULLS_FIRST;
    } else if (parser->current.type == TOKEN_LAST) {
        clause->nulls_position = NULLS_LAST;
    } else {
        error_at_current(parser, "Expected 'First' or 'Last'");
    }

    return clause;
}

static struct OrderByClausePtrList *collect_order_by_clauses(struct Parser *parser, struct Scanner *scanner) {
    struct OrderByClausePtrList *order_list = vector_order_by_clause_ptr_list_new();
    struct OrderByClause *order = parse_order_by_clause(parser, scanner);
    vector_order_by_clause_ptr_list_push(order_list, order);
   
    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        struct OrderByClause *order = parse_order_by_clause(parser, scanner);
        vector_order_by_clause_ptr_list_push(order_list, order);
    }

    return order_list;
}

static struct NewExprFunctionArgs *new_function_args() {
    struct NewExprFunctionArgs *args = malloc(sizeof(struct NewExprFunctionArgs));
    if (!args) {
        fprintf(stderr, ":new_function_args Could not malloc *args.\n");
        exit(1);
    }

    args->distinct  = false;
    args->star      = false;
    args->order_by  = NULL;
    args->exprs     = NULL;

    return args;
}

static struct NewExprPtrList *collect_expressions(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprPtrList *expr_list = vector_new_expr_ptr_list_new();
    struct NewExpr *expr = parse_expression_new(parser, scanner);
    vector_new_expr_ptr_list_push(expr_list, expr);
   
    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        struct NewExpr *expr = parse_expression_new(parser, scanner);
        vector_new_expr_ptr_list_push(expr_list, expr);
    }

    return expr_list;
}

static struct NewExprFunctionArgs *parse_function_arguments(struct Parser *parser, struct Scanner *scanner) {
    // Distinct
    // Star
    // Expr
    // Empty

    struct NewExprFunctionArgs *args = new_function_args();

    if (parser->current.type == TOKEN_RIGHT_PAREN) {
        // empty
        return args;
    } else if (parser->current.type == TOKEN_STAR) {
        args->star = true;
        return args;
    }

    // Expr
    if (parser->current.type == TOKEN_DISTINCT) {
        args->distinct = true;
        advance(parser, scanner);
    }

    args->exprs = collect_expressions(parser, scanner);

    if (parser->current.type != TOKEN_ORDER) {
        return args;
    }

    consume(parser, scanner, TOKEN_ORDER, "Expected 'ORDER'.");
    consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");

    args->order_by = collect_order_by_clauses(parser, scanner);

    return args;
}

static struct FilterClause *new_filter_clause() {
    struct FilterClause *filter = malloc(sizeof(struct FilterClause));
    if (!filter) {
        fprintf(stderr, "new_filter_clause: failed to malloc *filter.\n");
        exit(1);
    }

    return filter;
}

static struct FilterClause *parse_filter_clause(struct Parser *parser, struct Scanner *scanner) {
    struct FilterClause *filter = new_filter_clause();

    consume(parser, scanner, TOKEN_FILTER, "Expected 'FILTER'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    consume(parser, scanner, TOKEN_WHERE, "Expected 'WHERE'.");

    filter->expr = parse_expression_new(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    return filter;
}

static struct FrameSpec *new_frame_spec() {
    struct FrameSpec *frame_spec = malloc(sizeof(struct FrameSpec));
    if(!frame_spec) {
        fprintf(stderr, "new_frame_spec: failed to malloc *frame_spec.\n");
        exit(1);
    }

    frame_spec->start.offset    = NULL;
    frame_spec->end             = (struct FrameBound){ .type = FB_CURRENT_ROW, .offset = NULL };
    frame_spec->exclude         = EXCLUDE_NO_OTHERS;

    return frame_spec;
}

static struct FrameSpec *parse_frame_spec(struct Parser *parser, struct Scanner *scanner) {
    struct FrameSpec *frame_spec = new_frame_spec();

    switch (parser->current.type) {
        case TOKEN_RANGE:
            frame_spec->type = FRAME_RANGE;
            break;

        case TOKEN_ROWS:
            frame_spec->type = FRAME_ROWS;
            break;

        case TOKEN_GROUPS:
            frame_spec->type = FRAME_GROUPS;
            break;

        default:
            error_at_current(parser, "Unknown FrameType.");
    }

    advance(parser, scanner);

    // @TODO: this is very messy, can we clean up?
    if (parser->current.type == TOKEN_BETWEEN) {
        advance(parser, scanner);

        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                frame_spec->start.type = FB_UNBOUNDED_PRECEDING;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Exptected 'ROW'.");
                frame_spec->start.type = FB_CURRENT_ROW;
                break;

            default:
                frame_spec->start.offset = parse_expression_new(parser, scanner);
                if (parser->current.type == TOKEN_PRECEDING) {
                    advance(parser, scanner);
                    frame_spec->start.type = FB_N_PRECEDING;
                } else if (parser->current.type == TOKEN_FOLLOWING) {
                    advance(parser, scanner);
                    frame_spec->start.type = FB_N_FOLLOWING;
                } else {
                    error_at_current(parser, "Expected 'PRECEDING' or 'FOLLOWING'.");
                }
        }

        consume(parser, scanner, TOKEN_AND, "Expected 'AND'.");

        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_FOLLOWING, "Expected 'FOLLOWING'.");
                frame_spec->end.type = FB_UNBOUNDED_FOLLOWING;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Exptected 'ROW'.");
                frame_spec->end.type = FB_CURRENT_ROW;
                break;

            default:
                frame_spec->end.offset = parse_expression_new(parser, scanner);
                if (parser->current.type == TOKEN_PRECEDING) {
                    advance(parser, scanner);
                    frame_spec->end.type = FB_N_PRECEDING;
                } else if (parser->current.type == TOKEN_FOLLOWING) {
                    advance(parser, scanner);
                    frame_spec->end.type = FB_N_FOLLOWING;
                } else {
                    error_at_current(parser, "Expected 'PRECEDING' or 'FOLLOWING'.");
                }
        }

    } else {
        switch (parser->current.type) {
            case TOKEN_UNBOUNDED:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                frame_spec->start.type   = FB_UNBOUNDED_PRECEDING;
                frame_spec->end.type     = FB_CURRENT_ROW;
                break;

            case TOKEN_CURRENT:
                advance(parser, scanner);
                consume(parser, scanner, TOKEN_ROW, "Expected 'ROW'.");
                frame_spec->start.type = FB_CURRENT_ROW;
                frame_spec->end.type   = FB_CURRENT_ROW;
                break;

            default:
                frame_spec->start.offset = parse_expression_new(parser, scanner);
                consume(parser, scanner, TOKEN_PRECEDING, "Expected 'PRECEDING'.");
                frame_spec->start.type = FB_N_PRECEDING;
                frame_spec->end.type   = FB_CURRENT_ROW;
                break;
        }
    }

    // Exclude
    if (parser->current.type != TOKEN_EXCLUDE) {
        return frame_spec;
    }

    advance(parser, scanner);

    switch (parser->current.type) {
        case TOKEN_NO:
            advance(parser, scanner);
            consume(parser, scanner, TOKEN_OTHERS, "Expected 'OTHERS'.");
            frame_spec->exclude = EXCLUDE_NO_OTHERS;
            break;
        
        case TOKEN_CURRENT:
            advance(parser, scanner);
            consume(parser, scanner, TOKEN_ROW, "Expected 'ROW'.");
            frame_spec->exclude = EXCLUDE_CURRENT_ROW;
            break;

        case TOKEN_GROUP:
            advance(parser, scanner);
            frame_spec->exclude = EXCLUDE_GROUP;
            break;

        case TOKEN_TIES:
            advance(parser, scanner);
            frame_spec->exclude = EXCLUDE_TIES;
            break;

        default:
            error_at_current(parser, "Expected 'NO', 'CURRENT', 'GROUP' or 'TIES'.");
            
    }

    return frame_spec;
}

static struct WindowDefinition *new_window_definition() {
    struct WindowDefinition *definition = malloc(sizeof(struct WindowDefinition));
    if (!definition) {
        fprintf(stderr, "new_window_definition: failed to malloc *definition.\n");
        exit(1);
    }

    definition->base_window       = NULL;
    definition->partition_by      = NULL;
    definition->order_by          = NULL;
    definition->frame_spec        = NULL;

    return definition;
}

static struct WindowDefinition *parse_window_definition(struct Parser *parser, struct Scanner *scanner) {
    struct WindowDefinition *definition = new_window_definition();

    // Check for base-window-name
    if (parser->current.type == TOKEN_IDENTIFIER) {
        definition->base_window = new_unterminated_string();

        definition->base_window->start    = parser->current.start;
        definition->base_window->len      = parser->current.length;

        advance(parser, scanner);
    }

    if (parser->current.type == TOKEN_PARTITION) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        definition->partition_by = collect_expressions(parser, scanner);
    }

    if (parser->current.type == TOKEN_ORDER) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        definition->order_by = collect_order_by_clauses(parser, scanner);
    }

    if (parser->current.type == TOKEN_RANGE ||
        parser->current.type == TOKEN_ROWS ||
        parser->current.type == TOKEN_GROUPS)
        {
            definition->frame_spec = parse_frame_spec(parser, scanner);
        }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    return definition;
}

static struct OverClause *new_over_clause() {
    struct OverClause *over = malloc(sizeof(struct OverClause));
    if (!over) {
        fprintf(stderr, "new_over_clause: failed to malloc *over.\n");
        exit(1);
    }

    return over;
}

static struct OverClause *parse_over_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_OVER, "Expected 'OVER'.");

    struct OverClause *over = new_over_clause();

    if (parser->current.type == TOKEN_IDENTIFIER) {
        over->type = OVER_REFERENCED;
        
        over->referenced_window = new_unterminated_string();
        over->referenced_window->start  = parser->current.start;
        over->referenced_window->len    = parser->current.length;

        advance(parser, scanner);
        return over;
    }

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    over->type          = OVER_INLINE;
    over->inline_window = parse_window_definition(parser, scanner);

    return over;
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

    struct NewExprFunction *function = new_expr_function();

    function->name = (struct UnterminatedString){ .start = parser->current.start, .len = parser->current.length };

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
    function->args = parse_function_arguments(parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    // Check for filter clause
    if (parser->current.type == TOKEN_FILTER) {
        function->filter = parse_filter_clause(parser, scanner);
    }

    // Check for over clause
    if (parser->current.type == TOKEN_OVER) {
        function->over = parse_over_clause(parser, scanner);
    }

    return function;
}

static struct QualifiedName *new_qualified_name() {
    struct QualifiedName *name = malloc(sizeof(struct QualifiedName));
    if (!name) {
        fprintf(stderr, "new_qualified_name: failed to malloc *name.\n");
        exit(1);
    }

    name->count = 0;
    for (size_t i = 0; i < QUALIFIED_NAME_PARTS; i++) {
        name->parts[i] = (struct UnterminatedString){ .start = NULL, .len = 0 };
    }

    return name;
}

static struct QualifiedName *parse_name(struct Parser *parser, struct Scanner *scanner) {
    struct QualifiedName *name = new_qualified_name();

    if (peek(parser, scanner, 3)->type == TOKEN_DOT) {
        // schema-name
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
        struct UnterminatedString schema_name = { .start = parser->current.start, .len = parser->current.length };
        name->parts[name->count++] = schema_name;
        advance(parser, scanner);
    }
    
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        // table-name
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
        struct UnterminatedString table_name = { .start = parser->current.start, .len = parser->current.length };
        name->parts[name->count++] = table_name;
        advance(parser, scanner);

    }

    // column-name
    if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Expected identifier.");
    struct UnterminatedString column_name = { .start = parser->current.start, .len = parser->current.length };
    name->parts[name->count++] = column_name;
    advance(parser, scanner);

    return name;
}

static struct NewExprGrouping *new_expr_grouping() {
    struct NewExprGrouping *group = malloc(sizeof(struct NewExprGrouping));
    if (!group) {
        fprintf(stderr, "new_expr_grouping: failed to malloc *group.\n");
        exit(1);
    }

    return group;
}

static struct NewExprGrouping *parse_grouping(struct Parser *parser, struct Scanner *scanner) {
    struct NewExprGrouping *group = new_expr_grouping();
    group->inner = parse_expression_new(parser, scanner);
    return group;
}

static struct NewExprRaise *new_raise_expr() {
    struct NewExprRaise *raise = malloc(sizeof(struct NewExprRaise));
    if (!raise) {
        fprintf(stderr, "new_raise_expr: failed to malloc *raise.\n");
        exit(1);
    }

    raise->expr = NULL;

    return raise;
}

static struct NewExprRaise *parse_raise_expr(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_RAISE, "Expected 'RAISE'.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    struct NewExprRaise *raise = new_raise_expr();

    switch (parser->current.type) {

        case TOKEN_IGNORE:
            raise->type = RAISE_IGNORE;
            break;

        case TOKEN_ROLLBACK:
            consume(parser, scanner, TOKEN_COMMA, "Expected ','.");
            raise->type = RAISE_ROLLBACK;
            raise->expr = parse_expression_new(parser, scanner);
            break;

        case TOKEN_ABORT:
            raise->type = RAISE_ABORT;
            break;

        case TOKEN_FAIL:
            raise->type = RAISE_FAIL;
            break;

        default:
            error_at_current(parser, "Expected 'IGNORE', 'ROLLBACK', 'ABORT' or 'FAIL'.");
    }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    return raise;
}

static struct ResultColumn *new_result_column() {
    struct ResultColumn *result_column = malloc(sizeof(struct ResultColumn));
    if (!result_column) {
        fprintf(stderr, "new_result_column: failed to malloc *result_columns.\n");
        exit(1);
    }

    return result_column;
}

static struct ResultColumn *parse_result_column(struct Parser *parser, struct Scanner *scanner) {
    struct ResultColumn *result_column = new_result_column();

    if (parser->current.type == TOKEN_STAR) {
        advance(parser, scanner);
        result_column->type = RC_ALL;
        return result_column;
    }

    if (parser->current.type == TOKEN_IDENTIFIER && peek(parser, scanner, 1)->type == TOKEN_DOT) {
        result_column->type                         = RC_TABLE_ALL;
        result_column->table_all.table_name.start   = parser->current.start;
        result_column->table_all.table_name.len     = parser->current.length;
        
        advance(parser, scanner);
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_STAR, "Exptected '*'.");
        return result_column;
    }

    result_column->type         = RC_EXPR;
    result_column->expr.expr    = parse_expression_new(parser, scanner);
    result_column->expr.alias   = NULL;

    if (parser->current.type == TOKEN_AS) {
        advance(parser, scanner);
        if (parser->current.type != TOKEN_IDENTIFIER) error_at_current(parser, "Exptected identifier.");
        
        result_column->expr.alias->start    = parser->current.start;
        result_column->expr.alias->len      = parser->current.length;

        advance(parser, scanner);
    }

    return result_column;
}

static struct ResultColumnPtrList *collect_result_columns(struct Parser *parser, struct Scanner *scanner) {
    struct ResultColumnPtrList *result_column_ptr_list = vector_result_column_ptr_list_new();
    struct ResultColumn *result_column = parse_result_column(parser, scanner);
    vector_result_column_ptr_list_push(result_column_ptr_list, result_column);

    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        struct ResultColumn *result_column = parse_result_column(parser, scanner);
        vector_result_column_ptr_list_push(result_column_ptr_list, result_column);
    }

    return result_column_ptr_list;
}

static void tos_parse_alias(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    if (parser->current.type == TOKEN_AS) {
        advance(parser, scanner);
    }
    
    if (parser->current.type == TOKEN_IDENTIFIER) {
        // @TODO: are all following identifiers aliases?
        tos->alias        = new_unterminated_string();
        tos->alias->start = parser->current.start;
        tos->alias->len   = parser->current.length;
        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }
}

static struct TableName *new_table_name() {
    struct TableName *table_name = malloc(sizeof(struct TableName));
    if (!table_name) {
        fprintf(stderr, "new_table_name: failed to malloc *table_name.\n");
        exit(1);
    }

    return table_name;
}

static void tos_parse_table_name(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    tos->type                       = TOS_TABLE_NAME;
    tos->table_name                 = new_table_name();
    struct TableName *table_name    = tos->table_name;

    // schema-name
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        advance(parser, scanner);
        
        table_name->schema_name         = new_unterminated_string();
        table_name->schema_name->start  = parser->current.start;
        table_name->schema_name->len    = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }

    table_name->table_name.start     = parser->current.start;
    table_name->table_name.len       = parser->current.length;

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    tos_parse_alias(parser, scanner, tos);

    // Check for index
    if (parser->current.type == TOKEN_INDEXED) {
        consume(parser, scanner, TOKEN_INDEXED, "Expected 'INDEXED'.");
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");

        table_name->index_mode           = TABLE_INDEX_NAMED;
        table_name->index_name           = new_unterminated_string();
        table_name->index_name->start    = parser->current.start;
        table_name->index_name->len      = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    } else if (parser->current.type == TOKEN_NOT) {
        consume(parser, scanner, TOKEN_NOT, "Expected 'NOT'.");
        consume(parser, scanner, TOKEN_INDEXED, "Expected 'INDEXED'.");

        table_name->index_mode           = TABLE_INDEX_NONE;
        table_name->index_name           = new_unterminated_string();
        table_name->index_name->start    = parser->current.start;
        table_name->index_name->len      = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    } else {
        table_name->index_mode           = TABLE_INDEX_AUTO;
    }
}

static struct TableFunction *new_table_function() {
    struct TableFunction *table_function = malloc(sizeof(struct TableFunction));
    if (!table_function) {
        fprintf(stderr, "new_table_function: failed to malloc *table_function.\n");
        exit(1);
    }

    return table_function;
}

static void tos_parse_table_function(struct Parser *parser, struct Scanner *scanner, struct TableOrSubquery *tos) {
    tos->type                               = TOS_TABLE_FUNCTION;
    tos->table_function                     = new_table_function();
    struct TableFunction *table_function    = tos->table_function;

    // schema-name
    if (peek(parser, scanner, 1)->type == TOKEN_DOT) {
        advance(parser, scanner);
        
        table_function->schema_name         = new_unterminated_string();
        table_function->schema_name->start  = parser->current.start;
        table_function->schema_name->len    = parser->current.length;

        consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    }
    
    table_function->function_name.start  = parser->current.start;
    table_function->function_name.len    = parser->current.length;

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    table_function->args = collect_expressions(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    tos_parse_alias(parser, scanner, tos);

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

static struct LimitClause *new_limit_clause() {
    struct LimitClause *limit = malloc(sizeof(struct LimitClause));
    if (!limit) {
        fprintf(stderr, "new_limit_clause: failed to malloc *limit.\n");
        exit(1);
    }

    limit->offset = NULL;

    return limit;
}

static struct LimitClause *parse_limit_clause(struct Parser *parser, struct Scanner *scanner) {
    struct LimitClause *limit = new_limit_clause();

    struct NewExpr *expr = parse_expression_new(parser, scanner);

    if (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        limit->offset = expr;
        limit->limit  = parse_expression_new(parser, scanner);
    } else if (parser->current.type == TOKEN_OFFSET) {
        advance(parser, scanner);
        limit->offset = parse_expression_new(parser, scanner);
        limit->limit  = expr;
    } else {
        limit->limit  = expr;
    }

    return limit;
}

static struct SelectStatementNew *new_select_statement() {
    struct SelectStatementNew *stmt = malloc(sizeof(struct SelectStatementNew));
    if (!stmt) {
        fprintf(stderr, "new_select_statement: failed to malloc *stmt.\n");
        exit(1);
    }

    stmt->order = NULL;
    stmt->limit = NULL;

    return stmt;
}

static struct SelectStatementNew *parse_select_statement_new(struct Parser *parser, struct Scanner *scanner) {
    struct SelectStatementNew *stmt = new_select_statement();

    if (parser->current.type == TOKEN_WITH) {

    }

    stmt->cores = vector_select_core_data_ptr_list_new();
    
    struct SelectCoreData core_data = {
        .type = COMPOUND_OPERATOR_BASE,
        .core = parse_select_core(parser, scanner)
    };

    vector_select_core_data_ptr_list_push(stmt->cores, &core_data);

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

        struct SelectCoreData core_data = {
            .type = compound_type,
            .core = parse_select_core(parser, scanner)
        };

        vector_select_core_data_ptr_list_push(stmt->cores, &core_data);
    }

    if (parser->current.type == TOKEN_ORDER) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        
        stmt->order = collect_order_by_clauses(parser, scanner);
    }

    if (parser->current.type == TOKEN_LIMIT) {
        consume(parser, scanner, TOKEN_LIMIT, "Expected 'LIMIT'.");
        stmt->limit = parse_limit_clause(parser, scanner);
    }

    return stmt;
}


static struct TableOrSubquery *new_table_or_subquery() {
    struct TableOrSubquery *tos = malloc(sizeof(struct TableOrSubquery));
    if (!tos) {
        fprintf(stderr, "new_table_or_subquery: failed to malloc *tos.\n");
        exit(1);
    }

    tos->alias = NULL;

    return tos;
}

static bool is_start_of_select(struct Parser *parser) {
    return  parser->current.type == TOKEN_WITH
            || parser->current.type == TOKEN_SELECT
            || parser->current.type == TOKEN_VALUES;
}

static struct TableOrSubquery *parse_table_or_subquery(struct Parser *parser, struct Scanner *scanner) {
    struct TableOrSubquery *tos = new_table_or_subquery();

    if (is_start_of_select(parser)) {
        tos->type       = TOS_SUBQUERY;
        tos->subquery   = parse_select_statement_new(parser, scanner);
        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        tos_parse_alias(parser, scanner, tos);
        return tos;
    }

    // table-function-name(
    if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
        tos->type           = TOS_TABLE_FUNCTION;
        tos_parse_table_function(parser, scanner, tos);
        return tos;
    }

    // schema-name.table-function-name(
    if (peek(parser, scanner, 1)->type == TOKEN_DOT &&
        peek(parser, scanner, 3)->type == TOKEN_LEFT_PAREN) {

        tos->type           = TOS_TABLE_FUNCTION;
        tos_parse_table_function(parser, scanner, tos);
        return tos;
    }

    tos->type       = TOS_TABLE_NAME;
    tos_parse_table_name(parser, scanner, tos);

    return tos;
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

static struct JoinData *new_join_data() {
    struct JoinData *join_data = malloc(sizeof(struct JoinData));
    if (!join_data) {
        fprintf(stderr, "new_join_data: failed to malloc *join_data.\n");
        exit(1);
    }

    join_data->natural      = false;
    join_data->right        = NULL;
    join_data->constraint   = NULL;

    return join_data;
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

static struct JoinConstraint *new_join_constraint() {
    struct JoinConstraint *join_constraint = malloc(sizeof(struct JoinConstraint));
    if (!join_constraint) {
        fprintf(stderr, "new_join_constraint: failed to malloc *join_constraint.\n");
        exit(1);
    }

    return join_constraint;
}

static struct UnterminatedStringList *collect_unterminated_strings(struct Parser *parser, struct Scanner *scanner) {
    struct UnterminatedStringList *list = vector_unterminated_string_list_new();
    
    struct UnterminatedString string = { .start = parser->current.start, .len = parser->current.length };
    vector_unterminated_string_list_push(list, string);
    advance(parser, scanner);

    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        struct UnterminatedString string = { .start = parser->current.start, .len = parser->current.length };
        vector_unterminated_string_list_push(list, string);
        advance(parser, scanner);
    }

    return list;
}

static struct JoinClause *new_join_clause() {
    struct JoinClause *join_clause = malloc(sizeof(struct JoinClause));
    if (!join_clause) {
        fprintf(stderr, "new_join_clause: failed to malloc *join_clause.\n");
        exit(1);
    }

    return join_clause;
}

static struct JoinConstraint *parse_join_constraint(struct Parser *parser, struct Scanner *scanner) {
    struct JoinConstraint *join_constraint = NULL;
    
    switch (parser->current.type) {
        case TOKEN_ON:
            advance(parser, scanner);
            join_constraint = new_join_constraint();
            join_constraint->type = JOIN_CONSTRAINT_ON;
            join_constraint->expr = parse_expression_new(parser, scanner);
            break;

        case TOKEN_USING:
            advance(parser, scanner);
            consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");
            join_constraint = new_join_constraint();
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
    struct JoinClause *join_clause = new_join_clause();

    join_clause->left   = parse_table_expression(parser, scanner);
    join_clause->joins  = vector_join_data_ptr_list_new();

    while (is_start_of_join_operator(parser)) {
        struct JoinData *join_data = new_join_data();

        // parse join-operator
        join_data_parse_join_operator(parser, scanner, join_data);

        // parse table-or-subquery
        join_data->right = parse_table_expression(parser, scanner);

        // parse join-constraint
        join_data->constraint = parse_join_constraint(parser, scanner);

        if (join_data->natural && join_data->constraint != NULL) {
            fprintf(stderr, "join_data->constraint should be NULL if join_data->natural == true.\n");
            exit(1);
        }
    }

    return join_clause;
}

static struct TableExpression *new_table_expression() {
    struct TableExpression *table = malloc(sizeof(struct TableExpression));
    if (!table) {
        fprintf(stderr, "new_table_expression: failed ");
        exit(1);
    }

    return table;
}

static struct TableExpression *parse_table_expression(struct Parser *parser, struct Scanner *scanner) {
    struct TableExpression *table = new_table_expression();

    if (parser->current.type == TOKEN_IDENTIFIER) {
        table->type     = TE_SIMPLE;
        table->simple   = parse_table_or_subquery(parser, scanner);
        return table;
    }

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    if (is_start_of_select(parser)) {
        table->type     = TE_SIMPLE;
        table->simple   = parse_table_or_subquery(parser, scanner);
        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        return table;
    }

    // Join clause
    // Either explicit or comma separated list for cross joins
    table->type = TE_JOIN;
    table->join = parse_join_clause(parser, scanner);

    return table;    
}

static struct FromClause *new_from_clause() {
    struct FromClause *from = malloc(sizeof(struct FromClause));
    if (!from) {
        fprintf(stderr, "new_from_clause: failed to malloc *from.");
        exit(1);
    }

    return from;
}

static struct FromClause *parse_from_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_FROM, "Expected 'FROM'.");
    struct FromClause *from = new_from_clause();
    // Join clause can have a standalone table
    from->tables = parse_join_clause(parser, scanner);
    return from;
}

static struct WhereClause *new_where_clause() {
    struct WhereClause *where = malloc(sizeof(struct WhereClause));
    if (!where) {
        fprintf(stderr, "new_where_clause: failed to malloc *where.\n");
        exit(1);
    }

    return where;
}

static struct WhereClause *parse_where_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_WHERE, "Expected 'WHERE'.");
    struct WhereClause *where = new_where_clause();
    where->expr = parse_expression_new(parser, scanner);
    return where;
}

static struct GroupByClause *new_group_by_clause() {
    struct GroupByClause *group_by = malloc(sizeof(struct GroupByClause));
    if (!group_by) {
        fprintf(stderr, "new_group_by_clause: failed to malloc *group_by.\n");
        exit(1);
    }

    return group_by;
}

static struct GroupByClause *parse_group_by(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_GROUP, "Expected 'GROUP'.");
    consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
    struct GroupByClause *group_by = new_group_by_clause();
    group_by->expr_ptr_list = collect_expressions(parser, scanner);
    return group_by;
}

static struct HavingClause *new_having_clause() {
    struct HavingClause *having = malloc(sizeof(struct HavingClause));
    if (!having) {
        fprintf(stderr, "new_having_clause: failed to malloc *having.\n");
        exit(1);
    }

    return having;
}

static struct HavingClause *parse_having_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_HAVING, "Expected 'HAVING'");
    struct HavingClause *having = new_having_clause();
    having->expr = parse_expression_new(parser, scanner);
    return having;
}

static struct WindowData *new_window_data() {
    struct WindowData *window_data = malloc(sizeof(struct WindowData));
    if (!window_data) {
        fprintf(stderr, "new_window_data: failed to malloc *window_data.\n");
        exit(1);
    }

    return window_data;
}

static struct WindowData *parse_window_data(struct Parser *parser, struct Scanner *scanner) {
    struct WindowData *window_data = new_window_data();

    window_data->name.start = parser->current.start;
    window_data->name.len   = parser->current.length;

    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");
    consume(parser, scanner, TOKEN_AS, "Expected 'AS'.");

    window_data->definition = parse_window_definition(parser, scanner);

    return window_data;
}

static struct WindowDataPtrList *collect_window_data(struct Parser *parser, struct Scanner *scanner) {
    struct WindowDataPtrList *window_data_ptr_list = vector_window_data_ptr_list_new();
    struct WindowData *window_data = parse_window_data(parser, scanner);
    vector_window_data_ptr_list_push(window_data_ptr_list, window_data);

    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        struct WindowData *window_data = parse_window_data(parser, scanner);
        vector_window_data_ptr_list_push(window_data_ptr_list, window_data);
    }

    return window_data_ptr_list;
}

static struct WindowClause *new_window_clause() {
    struct WindowClause *window_clause = malloc(sizeof(struct WindowClause));
    if (!window_clause) {
        fprintf(stderr, "new_window_clause: failed to malloc *window_clause");
        exit(1);
    }

    return window_clause;
}

static struct WindowClause *parse_window_clause(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_WINDOW, "Expected 'WINDOW'.");
    struct WindowClause *window_clause = new_window_clause();

    window_clause->window_list = collect_window_data(parser, scanner);

    return window_clause;
}

static struct NewExprPtrListPtrList *collect_values(struct Parser *parser, struct Scanner *scanner) {
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    struct NewExprPtrListPtrList *list_of_lists = vector_new_expr_ptr_list_ptr_list_new();
    struct NewExprPtrList *list = collect_expressions(parser, scanner);
    vector_new_expr_ptr_list_ptr_list_push(list_of_lists, list);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    while (parser->current.type == TOKEN_COMMA) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

        struct NewExprPtrList *list = collect_expressions(parser, scanner);
        vector_new_expr_ptr_list_ptr_list_push(list_of_lists, list);

        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
    }

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
    struct NewExprRowValue *row_value_expr = new_row_value_expr();

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    row_value_expr->elements = collect_expressions(parser, scanner);

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

    return row_value_expr;
}

static struct SelectCore *new_select_core() {
    struct SelectCore *select_core = malloc(sizeof(struct SelectCore));
    if (!select_core) {
        fprintf(stderr, "new_select_core: failed to malloc *select_core.\n");
        exit(1);
    }

    select_core->select.distinct    = false;
    select_core->select.from        = NULL;
    select_core->select.where       = NULL;
    select_core->select.group_by    = NULL;
    select_core->select.having      = NULL;
    select_core->select.window      = NULL;
    select_core->values.values      = NULL;

    return select_core;
}

static struct SelectCore *parse_select_core(struct Parser *parser, struct Scanner *scanner) {
    struct SelectCore *select_core = new_select_core();

    if (parser->current.type == TOKEN_VALUES) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

        select_core->type           = SC_VALUES;
        select_core->values.values  = collect_values(parser, scanner);

        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");
        return select_core;
    }
    
    consume(parser, scanner, TOKEN_SELECT, "Expected 'SELECT'.");
    select_core->type = SC_SELECT;

    if (parser->current.type == TOKEN_DISTINCT) {
        select_core->select.distinct = true;
        advance(parser, scanner);
    } else if (parser->current.type == TOKEN_ALL) {
        advance(parser, scanner);
    }

    select_core->select.result_columns = collect_result_columns(parser, scanner);

    if (parser->current.type == TOKEN_FROM) {
        select_core->select.from = parse_from_clause(parser, scanner);
    }

    if (parser->current.type == TOKEN_WHERE) {
        select_core->select.where = parse_where_clause(parser, scanner);
    }

    if (parser->current.type == TOKEN_GROUP) {
        select_core->select.group_by = parse_group_by(parser, scanner);
        
        if (parser->current.type == TOKEN_HAVING) {
            select_core->select.having = parse_having_clause(parser, scanner);
        }
    }

    if (parser->current.type == TOKEN_WINDOW) {
        select_core->select.window = parse_window_clause(parser, scanner);
    }

    return select_core;
}

static struct NewExpr *new_new_expr() {
    struct NewExpr *expr = malloc(sizeof(struct NewExpr));
    if (!expr) {
        fprintf(stderr, "new_new_expr: failed to malloc *expr.\n");
        exit(1);
    }

    return expr;
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

    struct NewExpr *expr = new_new_expr();

    struct UnterminatedString text = { .start = parser->current.start, .len = 0 }; // Update length later

    switch (parser->current.type) {
        CASE_LITERAL
            expr->type      = EXPR_LITERAL;
            expr->literal   = parse_literal(parser, scanner);
            break;
        
        case TOKEN_CAST:
            expr->type      = EXPR_CAST;
            expr->cast      = parse_cast(parser, scanner);
            break;

        case TOKEN_CASE:
            expr->type      = EXPR_CASE;
            expr->expr_case = parse_case(parser, scanner);
            break;

        case TOKEN_IDENTIFIER:
            if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
                // Function
                expr->type      = EXPR_FUNC;
                expr->function  = parse_function(parser, scanner);
            } else {
                // schema-name or table-name or column-name
                expr->type = EXPR_NAME;
                expr->name = parse_name(parser, scanner);
            }

            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            advance(parser, scanner);
            if (is_start_of_select(parser))
                {
                    expr->type      = EXPR_SUBQUERY;
                    expr->subquery  = parse_select_statement_new(parser, scanner);
                // @TODO
                // select-stmt
                }
            else if (peek(parser, scanner, 1)->type == TOKEN_COMMA){
                // row value expr
                expr->type      = EXPR_ROW_VALUE;
                expr->row_value = parse_row_value_expr(parser, scanner);
            } else {
                // single expr
                expr->type      = EXPR_GROUPING;
                expr->grouping  = parse_grouping(parser, scanner);
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
            expr->type  = EXPR_RAISE;
            expr->raise = parse_raise_expr(parser, scanner);
            break;

        default:
            error_at_current(parser, "Expected Primary Expression");
    }

    text.len = parser->previous.start - text.start + parser->previous.length;
    expr->text = text;
    return expr;
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

static inline struct UnterminatedString unterminated_string_from_current_token(struct Parser *parser) {
            return (struct UnterminatedString){ .start = parser->current.start, .len = parser->current.length };
        }

static struct NewExprCollate *new_collate_expr() {
    struct NewExprCollate *collate = malloc(sizeof(struct NewExprCollate));
    if (!collate) {
        fprintf(stderr, "new_collate_expr: failed to malloc *collate.\n");
        exit(1);
    }

    return collate;
}


static struct NewExprCollate *parse_collate(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    consume(parser, scanner, TOKEN_COLLATE, "Expected 'COLLATE'.");
    
    struct NewExprCollate *collate = new_collate_expr();
    collate->expr = primary_expr;
    collate->collation_name = unterminated_string_from_current_token(parser);
    consume(parser, scanner, TOKEN_IDENTIFIER, "Expected identifier.");

    return collate;
}

static struct NewExprPatternMatch *new_pattern_match() {
    struct NewExprPatternMatch *match = malloc(sizeof(struct NewExprPatternMatch));
    if (!match) {
        fprintf(stderr, "new_pattern_match: failed to malloc *match.\n");
        exit(1);
    }

    match->escape = NULL;

    return match;
}

static struct NewExprPatternMatch *parse_pattern_match(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr, bool not) {
    struct NewExprPatternMatch *match = new_pattern_match();

    match->not  = not;
    match->left = primary_expr;

    // @TODO: must free
    switch (parser->current.type) {

        case TOKEN_GLOB:
            match->type     = PATTERN_GLOB;
            match->right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_REGEXP:
            match->type     = PATTERN_REGEXP;
            match->right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_MATCH:
            match->type     = PATTERN_MATCH;
            match->right    = parse_expression_new(parser, scanner);
            break;

        case TOKEN_LIKE:
            match->type     = PATTERN_LIKE;
            match->right    = parse_expression_new(parser, scanner);
            if (parser->current.type == TOKEN_ESCAPE) {
                advance(parser, scanner);
                match->escape = parse_expression_new(parser, scanner);
            }
            break;

        default:
            error_at_current(parser, "Expected 'GLOB', 'REGEXP', 'MATCH' or 'LIKE'.");
    }

    return match;
}

static struct NewExprBinary *new_binary_expr() {
    struct NewExprBinary *binary_expr = malloc(sizeof(struct NewExprBinary));
    if (!binary_expr) {
        fprintf(stderr, "new_binary_expr: failed to malloc *binary_expr.\n");
        exit(1);
    }

    return binary_expr;
}

static struct NewExprBinary *parse_binary_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprBinary *binary_expr = new_binary_expr();

    binary_expr->left = primary_expr;

    switch (parser->current.type) {
        case TOKEN_EQUAL:
            binary_expr->op = BIN_EQUAL;
            break;

        case TOKEN_GREATER:
            binary_expr->op = BIN_GREATER;
            break;

        case TOKEN_LESS:
            binary_expr->op = BIN_LESS;
            break;

        default:
            error_at_current(parser, "Expected binary operator");
    }

    binary_expr->right = parse_expression_new(parser, scanner);

    return binary_expr;
}

static struct NewExprNullComp *new_null_comp() {
    struct NewExprNullComp *null_comp = malloc(sizeof(struct NewExprNullComp));
    if (!null_comp) {
        fprintf(stderr, "new_null_comp: failed to malloc *null_comp.\n");
        exit(1);
    }

    return null_comp;
}

static struct NewExprNullComp *parse_null_comp(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprNullComp *null_comp = new_null_comp();

    null_comp->expr = primary_expr;

    switch (parser->current.type) {
        case TOKEN_ISNULL:
            null_comp->type = NULL_COMP_ISNULL;
            break;

        case TOKEN_NOTNULL:
            null_comp->type = NULL_COMP_NOTNULL;
            break;

        case TOKEN_NULL: // Not has already been consumed
            null_comp->type = NULL_COMP_NOTNULL;
            break;

        default:
            error_at_current(parser, "Expected 'ISNULL', 'NOTNULL' or 'NOT'.");
    }

    advance(parser, scanner);

    return null_comp;
}

static struct NewExprIs *new_is_expr() {
    struct NewExprIs *is_expr = malloc(sizeof(struct NewExprIs));
    if (!is_expr) {
        fprintf(stderr, "new_is_expr: failed to malloc *is_expr.\n");
        exit(1);
    }

    is_expr->not        = false;
    is_expr->distinct   = false;

    return is_expr;
}

static struct NewExprIs *parse_is_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr) {
    struct NewExprIs *is_expr = new_is_expr();

    consume(parser, scanner, TOKEN_IS, "Expected 'IS'.");

    if (parser->current.type == TOKEN_NOT) {
        is_expr->not = true;
        advance(parser, scanner);
    }

    if (parser->current.type == TOKEN_DISTINCT) {
        is_expr->distinct = true;
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_FROM, "Expected 'FROM'.");
    }

    is_expr->left   = primary_expr;
    is_expr->right  = parse_expression_new(parser, scanner);

    return is_expr;
}

static struct NewExprBetween *new_between_epxr() {
    struct NewExprBetween *between = malloc(sizeof(struct NewExprBetween));
    if (!between) {
        fprintf(stderr, "new_between_epxr: failed to malloc *between.\n");
        exit(1);
    }

    return between;
}

static struct NewExprBetween *parse_between_expr(struct Parser *parser, struct Scanner *scanner, struct NewExpr *primary_expr, bool not) {
    struct NewExprBetween *between = new_between_epxr();
    between->not        = not;
    between->primary    = primary_expr;

    consume(parser, scanner, TOKEN_BETWEEN, "Expected 'BETWEEN'.");

    between->left = parse_expression_new(parser, scanner);
    consume(parser, scanner, TOKEN_AND, "Expected 'AND'.");
    between->right = parse_expression_new(parser, scanner);

    return between;
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

    struct NewExpr *expr = new_new_expr();

    struct UnterminatedString text = { .start = parser->current.start, .len = 0 }; // Update length later

    switch (parser->current.type) {
        CASE_LITERAL
            expr->type      = EXPR_LITERAL;
            expr->literal   = parse_literal(parser, scanner);
            break;
        
        case TOKEN_CAST:
            expr->type      = EXPR_CAST;
            expr->cast      = parse_cast(parser, scanner);
            break;

        case TOKEN_CASE:
            expr->type      = EXPR_CASE;
            expr->expr_case = parse_case(parser, scanner);
            break;

        case TOKEN_IDENTIFIER:
            if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
                // Function
                expr->type      = EXPR_FUNC;
                expr->function  = parse_function(parser, scanner);
            } else {
                // schema-name or table-name or column-name
                expr->type = EXPR_NAME;
                expr->name = parse_name(parser, scanner);
            }

            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            advance(parser, scanner);
            if (is_start_of_select(parser))
                {
                    expr->type      = EXPR_SUBQUERY;
                    expr->subquery  = parse_select_statement_new(parser, scanner);
                // @TODO
                // select-stmt
                }
            else if (peek(parser, scanner, 1)->type == TOKEN_COMMA){
                // row value expr
                expr->type == EXPR_ROW_VALUE;
                expr->row_value = parse_row_value_expr(parser, scanner);
            } else {
                // single expr
                expr->type      = EXPR_GROUPING;
                expr->grouping  = parse_grouping(parser, scanner);
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
            expr->type  = EXPR_RAISE;
            expr->raise = parse_raise_expr(parser, scanner);
            break;

        default:
            // Must start with expr
            parse_secondary_expression(parser, scanner, expr);
            break;
    }

    text.len = parser->previous.start - text.start + parser->previous.length;
    expr->text = text;
    return expr;
}
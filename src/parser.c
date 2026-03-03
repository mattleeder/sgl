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

#include <stdint.h>

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

void parser_init(struct Parser *parser) {
    parser->head    = 0;
    parser->count   = 0;
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
    switch (parser->current.type) {

        case TOKEN_NUMBER: {
            int64_t value = string_to_int(parser->current.start, parser->current.length);
            return make_literal_number(value, text);
            break;
        }
            
        case TOKEN_STRING:
            // Cut off start and end quotes
            struct UnterminatedString value = { .start = parser->current.start + 1, .len = parser->current.length - 2};
            return make_literal_string(value, text);
            break;

        case TOKEN_NULL:
            return make_literal_null(text);
            break;

        case TOKEN_CURRENT_TIME:
            return make_literal_current_time(text);
            break;

        case TOKEN_CURRENT_DATE:
            return make_literal_current_date(text);
            break;

        case TOKEN_CURRENT_TIMESTAMP:
            return make_literal_current_timestamp(text);
            break;

        case TOKEN_TRUE:
            return make_literal_boolean(true, text);
            break;

        case TOKEN_FALSE:
            return make_literal_boolean(false, text);
            break;

        case TOKEN_BLOB:
            // Cut off starting X' and ending '
            struct LiteralBlob value = hex_string_to_bytes(parser->current.start + 2, parser->current.length - 3);       
            return make_literal_blob(value.start, value.len, text);
            break;

        default:
            fprintf(stderr, "parse_literal: unknown type %d\n", parser->current.type);
            error_at_current(parser, "Unknown type");
    }
}

#define CASE_TYPE_NAME              \
        case TOKEN_BLOB_KEYWORD:    \
        case TOKEN_INTEGER:         \
        case TOKEN_NULL:            \
        case TOKEN_REAL:            \
        case TOKEN_TEXT:

static enum CastType parse_type_name(struct Parser *parser, struct Scanner *scanner) {
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

static struct NewExprCast *new_cast_expr(struct Parser *parser, struct Scanner *scanner) {
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

    struct NewExprCast *cast_expr = new_cast_expr(parser, scanner);

    cast_expr->expr         = expr;
    cast_expr->cast_type    = cast_type;

    return cast_expr;
}

static struct NewExprCase *new_expr_case() {
    struct NewExprCase *expr_case = malloc(sizeof(struct NewExprCase));
    if (!expr_case) {
        fprint(stderr, "new_expr_case: failed to malloc *case.\n");
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

static struct PrimaryExpr *new_primary_expr() {
    struct PrimaryExpr *expr = malloc(sizeof(struct PrimaryExpr));
    if (!expr) {
        fprintf(stderr, "new_primary_expr: failed to malloc *expr.\n");
        exit(1);
    }

    return expr;
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

    filter->expr = parse_expression(parser, scanner);

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
                frame_spec->start.offset = FB_N_PRECEDING;
                frame_spec->end.offset   = FB_CURRENT_ROW;
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

static struct OverClause *new_over_clause() {
    struct OverClause *over = malloc(sizeof(struct OverClause));
    if (!over) {
        fprintf(stderr, "new_over_clause: failed to malloc *over.\n");
        exit(1);
    }

    over->referenced_window = NULL;
    over->base_window       = NULL;
    over->partition_by      = NULL;
    over->order_by          = NULL;
    over->frame_spec        = NULL;

    return over;
}

static struct OverClause *parse_over_clause(struct Parser *parser, struct Scanner *scanner) {
    struct OverClause *over = new_over_clause();

    if (parser->current.type == TOKEN_IDENTIFIER) {
        over->referenced_window = new_unterminated_string();
        
        over->referenced_window->start  = parser->current.start;
        over->referenced_window->len    = parser->current.length;

        advance(parser, scanner);
        return over;
    }

    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expected '('.");

    // Check for base-window-name
    if (parser->current.type == TOKEN_IDENTIFIER) {
        over->base_window = new_unterminated_string();

        over->base_window->start    = parser->current.start;
        over->base_window->len      = parser->current.length;

        advance(parser, scanner);
    }

    if (parser->current.type == TOKEN_PARTITION) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        over->partition_by = collect_expressions(parser, scanner);
    }

    if (parser->current.type == TOKEN_ORDER) {
        advance(parser, scanner);
        consume(parser, scanner, TOKEN_BY, "Expected 'BY'.");
        over->order_by = collect_order_by_clauses(parser, scanner);
    }

    if (parser->current.type == TOKEN_RANGE ||
        parser->current.type == TOKEN_ROWS ||
        parser->current.type == TOKEN_GROUPS)
        {
            over->frame_spec = parse_frame_spec(parser, scanner);
        }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expected ')'.");

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

static struct PrimaryExpr *parse_primary_expression(struct Parser *parser, struct Scanner *scanner) {
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
    */

    struct PrimaryExpr *expr = new_primary_expr();

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
                expr->type      = EXPR_FUNCTION;
                expr->function  = parse_function(parser, scanner);
            } else {
                // schema-name or table-name or column-name
            }
            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            if (peek(parser, scanner, 1)->type == TOKEN_WITH
                || peek(parser, scanner, 1)->type == TOKEN_SELECT
                || peek(parser, scanner, 1)->type == TOKEN_VALUES) 
                {
                // select-stmt
                }
            else {
                // expr-list
            }
            break;

        case TOKEN_NOT:
            // Fall through if peek(1) not 'EXISTS'
            if (peek(parser, scanner, 1)->type == TOKEN_EXISTS) {
                break;
            }

        case TOKEN_EXISTS:
            break;

        case TOKEN_RAISE:
            break;

        default:
            error_at_current(parser, "Expected Primary Expression");
    }

    text.len = parser->previous.start - text.start + parser->previous.length;
    expr->text = text;
    return expr;
}

static struct NewExpr *parse_expression_new(struct Parser *parser, struct Scanner *scanner) {
    /* 
    Expect:
        Literal
        Bind parameter
        Schema-name or table-name or column-name
        Unary operator
        Function-name -> (
        ( -> expr or ( -> select-stmt (TOKEN_WITH, TOKEN_SELECT, TOKEN_VALUES)
        Cast
        NOT
        EXISTS
        Case
        Raise
    */

    // Parse primary expression
    switch (parser->current.type) {
        CASE_LITERAL
            parse_literal(parser, scanner);
            break;

        CASE_UNARY
            parse_unary(parser, scanner);
            break;
        
        case TOKEN_CAST:
            break;

        case TOKEN_CASE:
            break;

        case TOKEN_IDENTIFIER:
            if (peek(parser, scanner, 1)->type == TOKEN_LEFT_PAREN) {
                // Function
            } else {
                // schema-name or table-name or column-name
            }
            break;

        case TOKEN_LEFT_PAREN: // Can have both (expr) and (select-stmt)
            if (peek(parser, scanner, 1)->type == TOKEN_WITH
                || peek(parser, scanner, 1)->type == TOKEN_SELECT
                || peek(parser, scanner, 1)->type == TOKEN_VALUES) 
                {
                // select-stmt
                }
            else {
                // expr-list
            }
            break;

        case TOKEN_NOT:
            // Fall through if peek(1) not 'EXISTS'
            if (peek(parser, scanner, 1)->type == TOKEN_EXISTS) {
                break;
            }

        case TOKEN_EXISTS:
            break;

        case TOKEN_RAISE:
            break;

        default:
            error_at_current(parser, "Expected Primary Expression");
    }
}

parse_result_column(struct Parser *parser, struct Scanner *scanner) {
    if (parser->current.type == TOKEN_STAR) {
        // Only star
    } else if (peek(parser, scanner, 2) == TOKEN_STAR) {
        // TableName.star
    } else {
        // Expr
        // Possibly AS
        // Possibly column-alias
    }
}

// parse_result_columns(struct Parser *parser, struct Scanner *scanner) {

// }

// parse_select_core(struct Parser *parser, struct Scanner *scanner) {
//     consume(parser, scanner, TOKEN_SELECT, "Expected 'SELECT'.");

//     if (parser->current.type == TOKEN_DISTINCT) {
//         advance(parser, scanner);
//     } else if (parser->current.type == TOKEN_ALL) {
//         advance(parser, scanner);
//     }


// }
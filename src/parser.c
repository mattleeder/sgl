#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "token.h"
#include "lexer.h"
#include "trie.h"
#include "common.h"
#include "parser.h"
#include "ast.h"
#include "sql_utils.h"
#include "memory.h"

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

// parse_result_column(struct Parser *parser, struct Scanner *scanner) {
//     if (parser->current.type == TOKEN_STAR) {

//     } else if 
// }

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
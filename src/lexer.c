#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "token.h"
#include "trie.h"
#include "lexer.h"

static const struct ReservedWord reserved_words[] = {
    { .word = "SELECT",     .type = TOKEN_SELECT },
    { .word = "FROM",       .type = TOKEN_FROM },
    { .word = "WHERE",      .type = TOKEN_WHERE },
    { .word = "AND",        .type = TOKEN_AND },
    { .word = "CREATE",     .type = TOKEN_CREATE },
    { .word = "TABLE",      .type = TOKEN_TABLE },
    { .word = "INTEGER",    .type = TOKEN_INTEGER },
    { .word = "PRIMARY",    .type = TOKEN_PRIMARY },
    { .word = "KEY",        .type = TOKEN_KEY },
    { .word = "TEXT",       .type = TOKEN_TEXT },
    { .word = "UNIQUE",     .type = TOKEN_UNIQUE },
    { .word = "IF",         .type = TOKEN_IF },
    { .word = "NOT",        .type = TOKEN_NOT },
    { .word = "EXISTS",     .type = TOKEN_EXISTS },
    { .word = "ON",         .type = TOKEN_ON},
    { .word = "INDEX",      .type = TOKEN_INDEX }
};

void init_scanner(struct Scanner *scanner, const char* source, struct TriePool *reserved_words_pool) {
    scanner->reserved_words_pool    = reserved_words_pool;
    scanner->start                  = source;
    scanner->current                = source;
    scanner->line                   = 1;
}

static void add_reserved_words_to_pool(struct TriePool *pool) {
    for (size_t i = 0; i < sizeof(reserved_words) / sizeof(reserved_words[0]); i++) {
        add_word_to_trie(pool, reserved_words[i].word, strlen(reserved_words[i].word), reserved_words[i].type);
    }
}

struct TriePool *init_reserved_words() {
    struct TriePool *pool = create_trie();
    add_reserved_words_to_pool(pool);
    return pool;
}

static struct Token make_token(struct Scanner *scanner, enum TokenType type) {
    struct Token token;
    token.type      = type;
    token.start     = scanner->start;
    token.length    = (int)(scanner->current - scanner->start);
    token.line      = scanner->line;
    return token;
}

static struct Token error_token(struct Scanner *scanner, const char* message) {
    struct Token token;
    token.type      = TOKEN_ERROR;
    token.start     = message;
    token.length    = (int)strlen(message);
    token.line      = scanner->line;
    return token;
}

static bool is_at_end(struct Scanner *scanner) {
    return *scanner->current == '\0';
}

static char advance(struct Scanner *scanner) {
    scanner->current++;
    return scanner->current[-1];
}

static bool match(struct Scanner *scanner, char expected) {
    if (is_at_end(scanner)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    return true;
}

static char peek(struct Scanner *scanner) {
    return *scanner->current;
}

static char peek_next(struct Scanner *scanner) {
    if (is_at_end(scanner)) return '\0';
    return scanner->current[1];
}

static void skip_whitespace(struct Scanner *scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;
            case '\n':
                scanner->line++;
                advance(scanner);
                break;
            case '/':
                if (peek_next(scanner) == '/') {
                    // A comment goes until the end of the line.
                    while (peek(scanner) != '\n' && !is_at_end(scanner)) advance(scanner);
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static struct Token string(struct Scanner *scanner, char terminator) {
    while (peek(scanner) != terminator && !is_at_end(scanner)) {
        if (peek(scanner) == '\n') scanner->line++;
        advance(scanner);
    }

    if (is_at_end(scanner)) return error_token(scanner, "Unterminated string.");

    // The closing quote.
    advance(scanner);
    fprintf(stderr, "TOKEN_STRING: %.*s\n", (int)(scanner->current - scanner->start), scanner->start);
    return make_token(scanner, TOKEN_STRING);
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static struct Token number(struct Scanner *scanner) {
    fprintf(stderr, "Found number at %p\n", scanner->start);
    while (is_digit(peek(scanner))) advance(scanner);

    // Look for a fractional part.
    if (peek(scanner) == '.' && is_digit(peek_next(scanner))) {
        // Consume the ".".
        advance(scanner);

        while (is_digit(peek(scanner))) advance(scanner);
    }

    return make_token(scanner, TOKEN_NUMBER);
}

static enum TokenType identifier_type(struct Scanner *scanner) {
    enum TokenType token;
    if (is_word_in_trie(scanner->reserved_words_pool, scanner->start, scanner->current - scanner->start, &token)) {
        return token;
    }
    return TOKEN_IDENTIFIER;
}

static bool is_alpha(char c) {
    return  (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_';
}

static struct Token identifier(struct Scanner *scanner) {
    while (is_alpha(peek(scanner)) || is_digit(peek(scanner))) advance(scanner);
    return make_token(scanner, identifier_type(scanner));
}

struct Token scan_token(struct Scanner *scanner) {
    skip_whitespace(scanner);
    scanner->start = scanner->current;

    if (is_at_end(scanner)) return make_token(scanner, TOKEN_EOF);

    char c = advance(scanner);
    if (is_alpha(c)) return identifier(scanner);
    if (is_digit(c)) return number(scanner);

    switch (c) {

        case '(': return make_token(scanner, TOKEN_LEFT_PAREN);
        case ')': return make_token(scanner, TOKEN_RIGHT_PAREN);
        case ';': return make_token(scanner, TOKEN_SEMICOLON);
        case ',': return make_token(scanner, TOKEN_COMMA);
        case '.': return make_token(scanner, TOKEN_DOT);
        case '-': return make_token(scanner, TOKEN_MINUS);
        case '+': return make_token(scanner, TOKEN_PLUS);
        case '/': return make_token(scanner, TOKEN_SLASH);
        case '*': return make_token(scanner, TOKEN_STAR);
        case '=': return make_token(scanner, TOKEN_EQUAL);
        case '!':
        return make_token(scanner,
            match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);

        case '<':
        return make_token(scanner,
            match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);

        case '>':
        return make_token(scanner,
            match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
            
        case '\'':
        case '\"':
        return string(scanner, c);
    }

    char *buffer = malloc(32);
    if (!buffer) {
        fprintf(stderr, "scan_token: *buffer malloc failed\n");
        exit(1);
    }
    
    sprintf(buffer, "Unexpected character: %c.", c);
    return error_token(scanner, buffer);
}

void tokenize(struct Scanner *scanner, const char* source) {
    struct TriePool *pool = init_reserved_words();
    fprintf(stderr, "Initialised reserved words\n");
    init_scanner(scanner, source, pool);
    fprintf(stderr, "Scanner is at %p\n", scanner->start);
    int line = -1;

    for (;;) {
        struct Token token = scan_token(scanner);
        if (token.line != line) {
            fprintf(stderr, "%4d| ", token.line);
            line = token.line;
        } else {
            fprintf(stderr, "    | ");
        }
        fprintf(stderr, "%2d '%.*s'\n", token.type, token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }

    fprintf(stderr, "Tokenization complete\n");
}
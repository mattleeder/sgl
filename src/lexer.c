#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "token.h"
#include "trie.h"
#include "lexer.h"

static const struct ReservedWord reserved_words[] = {
    { .word = "ABORT",             .type = TOKEN_ABORT             },
    { .word = "ACTION",            .type = TOKEN_ACTION            },
    { .word = "ADD",               .type = TOKEN_ADD               },
    { .word = "AFTER",             .type = TOKEN_AFTER             },
    { .word = "ALL",               .type = TOKEN_ALL               },
    { .word = "ALTER",             .type = TOKEN_ALTER             },
    { .word = "ALWAYS",            .type = TOKEN_ALWAYS            },
    { .word = "ANALYZE",           .type = TOKEN_ANALYZE           },
    { .word = "AND",               .type = TOKEN_AND               },
    { .word = "AS",                .type = TOKEN_AS                },
    { .word = "ASC",               .type = TOKEN_ASC               },
    { .word = "ATTACH",            .type = TOKEN_ATTACH            },
    { .word = "AUTOINCREMENT",     .type = TOKEN_AUTOINCREMENT     },
    { .word = "BEFORE",            .type = TOKEN_BEFORE            },
    { .word = "BEGIN",             .type = TOKEN_BEGIN             },
    { .word = "BETWEEN",           .type = TOKEN_BETWEEN           },
    { .word = "BY",                .type = TOKEN_BY                },
    { .word = "CASCADE",           .type = TOKEN_CASCADE           },
    { .word = "CASE",              .type = TOKEN_CASE              },
    { .word = "CAST",              .type = TOKEN_CAST              },
    { .word = "CHECK",             .type = TOKEN_CHECK             },
    { .word = "COLLATE",           .type = TOKEN_COLLATE           },
    { .word = "COLUMN",            .type = TOKEN_COLUMN            },
    { .word = "COMMIT",            .type = TOKEN_COMMIT            },
    { .word = "CONFLICT",          .type = TOKEN_CONFLICT          },
    { .word = "CONSTRAINT",        .type = TOKEN_CONSTRAINT        },
    { .word = "CREATE",            .type = TOKEN_CREATE            },
    { .word = "CROSS",             .type = TOKEN_CROSS             },
    { .word = "CURRENT",           .type = TOKEN_CURRENT           },
    { .word = "CURRENT_DATE",      .type = TOKEN_CURRENT_DATE      },
    { .word = "CURRENT_TIME",      .type = TOKEN_CURRENT_TIME      },
    { .word = "CURRENT_TIMESTAMP", .type = TOKEN_CURRENT_TIMESTAMP },
    { .word = "DATABASE",          .type = TOKEN_DATABASE          },
    { .word = "DEFAULT",           .type = TOKEN_DEFAULT           },
    { .word = "DEFERRABLE",        .type = TOKEN_DEFERRABLE        },
    { .word = "DEFERRED",          .type = TOKEN_DEFERRED          },
    { .word = "DELETE",            .type = TOKEN_DELETE            },
    { .word = "DESC",              .type = TOKEN_DESC              },
    { .word = "DETACH",            .type = TOKEN_DETACH            },
    { .word = "DISTINCT",          .type = TOKEN_DISTINCT          },
    { .word = "DO",                .type = TOKEN_DO                },
    { .word = "DROP",              .type = TOKEN_DROP              },
    { .word = "EACH",              .type = TOKEN_EACH              },
    { .word = "ELSE",              .type = TOKEN_ELSE              },
    { .word = "END",               .type = TOKEN_END               },
    { .word = "ESCAPE",            .type = TOKEN_ESCAPE            },
    { .word = "EXCEPT",            .type = TOKEN_EXCEPT            },
    { .word = "EXCLUDE",           .type = TOKEN_EXCLUDE           },
    { .word = "EXCLUSIVE",         .type = TOKEN_EXCLUSIVE         },
    { .word = "EXISTS",            .type = TOKEN_EXISTS            },
    { .word = "EXPLAIN",           .type = TOKEN_EXPLAIN           },
    { .word = "FAIL",              .type = TOKEN_FAIL              },
    { .word = "FILTER",            .type = TOKEN_FILTER            },
    { .word = "FIRST",             .type = TOKEN_FIRST             },
    { .word = "FOLLOWING",         .type = TOKEN_FOLLOWING         },
    { .word = "FOR",               .type = TOKEN_FOR               },
    { .word = "FOREIGN",           .type = TOKEN_FOREIGN           },
    { .word = "FROM",              .type = TOKEN_FROM              },
    { .word = "FULL",              .type = TOKEN_FULL              },
    { .word = "GENERATED",         .type = TOKEN_GENERATED         },
    { .word = "GLOB",              .type = TOKEN_GLOB              },
    { .word = "GROUP",             .type = TOKEN_GROUP             },
    { .word = "GROUPS",            .type = TOKEN_GROUPS            },
    { .word = "HAVING",            .type = TOKEN_HAVING            },
    { .word = "IF",                .type = TOKEN_IF                },
    { .word = "IGNORE",            .type = TOKEN_IGNORE            },
    { .word = "IMMEDIATE",         .type = TOKEN_IMMEDIATE         },
    { .word = "IN",                .type = TOKEN_IN                },
    { .word = "INDEX",             .type = TOKEN_INDEX             },
    { .word = "INDEXED",           .type = TOKEN_INDEXED           },
    { .word = "INITIALLY",         .type = TOKEN_INITIALLY         },
    { .word = "INNER",             .type = TOKEN_INNER             },
    { .word = "INSERT",            .type = TOKEN_INSERT            },
    { .word = "INSTEAD",           .type = TOKEN_INSTEAD           },
    { .word = "INTERSECT",         .type = TOKEN_INTERSECT         },
    { .word = "INTO",              .type = TOKEN_INTO              },
    { .word = "IS",                .type = TOKEN_IS                },
    { .word = "ISNULL",            .type = TOKEN_ISNULL            },
    { .word = "JOIN",              .type = TOKEN_JOIN              },
    { .word = "KEY",               .type = TOKEN_KEY               },
    { .word = "LAST",              .type = TOKEN_LAST              },
    { .word = "LEFT",              .type = TOKEN_LEFT              },
    { .word = "LIKE",              .type = TOKEN_LIKE              },
    { .word = "LIMIT",             .type = TOKEN_LIMIT             },
    { .word = "MATCH",             .type = TOKEN_MATCH             },
    { .word = "MATERIALIZED",      .type = TOKEN_MATERIALIZED      },
    { .word = "NATURAL",           .type = TOKEN_NATURAL           },
    { .word = "NO",                .type = TOKEN_NO                },
    { .word = "NOT",               .type = TOKEN_NOT               },
    { .word = "NOTHING",           .type = TOKEN_NOTHING           },
    { .word = "NOTNULL",           .type = TOKEN_NOTNULL           },
    { .word = "NULL",              .type = TOKEN_NULL              },
    { .word = "NULLS",             .type = TOKEN_NULLS             },
    { .word = "OF",                .type = TOKEN_OF                },
    { .word = "OFFSET",            .type = TOKEN_OFFSET            },
    { .word = "ON",                .type = TOKEN_ON                },
    { .word = "OR",                .type = TOKEN_OR                },
    { .word = "ORDER",             .type = TOKEN_ORDER             },
    { .word = "OTHERS",            .type = TOKEN_OTHERS            },
    { .word = "OUTER",             .type = TOKEN_OUTER             },
    { .word = "OVER",              .type = TOKEN_OVER              },
    { .word = "PARTITION",         .type = TOKEN_PARTITION         },
    { .word = "PLAN",              .type = TOKEN_PLAN              },
    { .word = "PRAGMA",            .type = TOKEN_PRAGMA            },
    { .word = "PRECEDING",         .type = TOKEN_PRECEDING         },
    { .word = "PRIMARY",           .type = TOKEN_PRIMARY           },
    { .word = "QUERY",             .type = TOKEN_QUERY             },
    { .word = "RAISE",             .type = TOKEN_RAISE             },
    { .word = "RANGE",             .type = TOKEN_RANGE             },
    { .word = "RECURSIVE",         .type = TOKEN_RECURSIVE         },
    { .word = "REFERENCES",        .type = TOKEN_REFERENCES        },
    { .word = "REGEXP",            .type = TOKEN_REGEXP            },
    { .word = "REINDEX",           .type = TOKEN_REINDEX           },
    { .word = "RELEASE",           .type = TOKEN_RELEASE           },
    { .word = "RENAME",            .type = TOKEN_RENAME            },
    { .word = "REPLACE",           .type = TOKEN_REPLACE           },
    { .word = "RESTRICT",          .type = TOKEN_RESTRICT          },
    { .word = "RETURNING",         .type = TOKEN_RETURNING         },
    { .word = "RIGHT",             .type = TOKEN_RIGHT             },
    { .word = "ROLLBACK",          .type = TOKEN_ROLLBACK          },
    { .word = "ROW",               .type = TOKEN_ROW               },
    { .word = "ROWS",              .type = TOKEN_ROWS              },
    { .word = "SAVEPOINT",         .type = TOKEN_SAVEPOINT         },
    { .word = "SELECT",            .type = TOKEN_SELECT            },
    { .word = "SET",               .type = TOKEN_SET               },
    { .word = "TABLE",             .type = TOKEN_TABLE             },
    { .word = "TEMP",              .type = TOKEN_TEMP              },
    { .word = "TEMPORARY",         .type = TOKEN_TEMPORARY         },
    { .word = "THEN",              .type = TOKEN_THEN              },
    { .word = "TIES",              .type = TOKEN_TIES              },
    { .word = "TO",                .type = TOKEN_TO                },
    { .word = "TRANSACTION",       .type = TOKEN_TRANSACTION       },
    { .word = "TRIGGER",           .type = TOKEN_TRIGGER           },
    { .word = "UNBOUNDED",         .type = TOKEN_UNBOUNDED         },
    { .word = "UNION",             .type = TOKEN_UNION             },
    { .word = "UNIQUE",            .type = TOKEN_UNIQUE            },
    { .word = "UPDATE",            .type = TOKEN_UPDATE            },
    { .word = "USING",             .type = TOKEN_USING             },
    { .word = "VACUUM",            .type = TOKEN_VACUUM            },
    { .word = "VALUES",            .type = TOKEN_VALUES            },
    { .word = "VIEW",              .type = TOKEN_VIEW              },
    { .word = "VIRTUAL",           .type = TOKEN_VIRTUAL           },
    { .word = "WHEN",              .type = TOKEN_WHEN              },
    { .word = "WHERE",             .type = TOKEN_WHERE             },
    { .word = "WINDOW",            .type = TOKEN_WINDOW            },
    { .word = "WITH",              .type = TOKEN_WITH              },
    { .word = "WITHOUT",           .type = TOKEN_WITHOUT           }
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
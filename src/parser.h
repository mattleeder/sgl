#ifndef sql_parser
#define sql_parser

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "token.h"
#include "trie.h"

#define TOKEN_BUFFER_SIZE (4)
#define DEFAULT_ARENA_CAPACITY ((size_t)(4 * 1024)) // 4KB

struct Parser {
    struct Token    buffer[TOKEN_BUFFER_SIZE];
    size_t          head;
    size_t          count;

    struct Token    current;
    struct Token    previous;
    bool            had_error;
    bool            panic_mode;

    struct ArenaAllocator arena;
};

void parser_init(struct Parser *parser, size_t arena_capacity);
struct SelectStatement *parse(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct Columns *parse_create(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct CreateIndexStatement *parse_create_index(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);

#endif
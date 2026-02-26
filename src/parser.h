#ifndef sql_parser
#define sql_parser

#include "ast.h"
#include "lexer.h"
#include "token.h"
#include "trie.h"

struct Parser {
    struct Token    current;
    struct Token    previous;
    bool            had_error;
    bool            panic_mode;
};

struct SelectStatement *parse(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct Columns *parse_create(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct CreateIndexStatement *parse_create_index(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);

#endif
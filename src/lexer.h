#ifndef sql_lexer
#define sql_lexer

#include "trie.h"

struct Scanner {
    struct TriePool *reserved_words_pool;
    const char      *start;
    const char      *current;
    int             line;
};

struct TriePool *init_reserved_words();
void tokenize(struct Scanner *scanner, const char* source);
struct Token scan_token(struct Scanner *scanner);
void init_scanner(struct Scanner *scanner, const char* source, struct TriePool *reserved_words_pool);

#endif
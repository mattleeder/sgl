#ifndef sql_trie
#define sql_trie

#include <stdbool.h>

#include "token.h"

struct TriePool *create_trie();
bool add_word_to_trie(struct TriePool *pool, const char *start, size_t length, enum TokenType token_type);
bool is_word_in_trie(struct TriePool *pool, const char *start, size_t length, enum TokenType *token_type);

#endif
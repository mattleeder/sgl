#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "token.h"

/* 
The upper bound for the amount of nodes we need
is the sum of all the string lengths
*/
#define MAX_NODES       (1000)
#define ALPHABET_SIZE   (26)
#define EMPTY_CHILD     (-1)

struct Trie {
    int16_t             sub_tries[ALPHABET_SIZE]; // Index to the node pool, -1 means absent
    bool                end_of_word;
    enum TokenType      token_type;
};

struct TriePool {
    struct Trie     nodes[MAX_NODES];
    int             size;
};

static int new_node(struct TriePool *pool) {
    if (pool->size >= MAX_NODES) {
        fprintf(stderr, "new_node: Ran out of nodes\n");
        exit(1); // Out of nodes
    }
    
    int idx = pool->size++;
    for (int i = 0; i < ALPHABET_SIZE; i++) pool->nodes[idx].sub_tries[i] = EMPTY_CHILD;
    pool->nodes[idx].end_of_word = false;
    pool->nodes[idx].token_type = TOKEN_ERROR;
    return idx;
}

static struct TriePool *create_pool() {
    struct TriePool *pool = calloc(1, sizeof(struct TriePool));
    if (pool == NULL) {
        fprintf(stderr, "create_pool: calloc failed.\n");
        exit(1);
    }
    pool->size = 0;
    return pool;
}

struct TriePool *create_trie() {
    struct TriePool *pool = create_pool();
    new_node(pool); // Create head
    return pool;
}

static int char_to_index(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    } else if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }

    fprintf(stderr, "Character is not alphabetic\n");
    return -1;
}

bool add_word_to_trie(struct TriePool *pool, const char *start, size_t length, enum TokenType token_type) {
    int curr = 0; // root node
    struct Trie *nodes = pool->nodes;

    for (int i = 0; i < length; i++) { 
        char c = start[i];
        int index = char_to_index(c);

        if (index < 0 || index >= ALPHABET_SIZE) {
            return false;
        }

        if (nodes[curr].sub_tries[index] == EMPTY_CHILD) {
            int next = new_node(pool);
            if (next == -1) {
                fprintf(stderr, "Pool exhausted\n");
                exit(1);
            }

            nodes[curr].sub_tries[index] = next;
        }
        curr = nodes[curr].sub_tries[index];
        if (curr >= pool->size) {
            fprintf(stderr, "add_word_to_trie: Trie node %d out of bounds %d\n", curr, pool->size);
            exit(1);
        }
    }

    nodes[curr].end_of_word = true;
    nodes[curr].token_type  = token_type;
    return true;
}

bool is_word_in_trie(const struct TriePool *pool, const char *start, size_t length, enum TokenType *token_type) {
    int curr = 0; // root node
    const struct Trie *nodes = pool->nodes;

    for (int i = 0; i < length; i++) { 
        char c = start[i];
        int index = char_to_index(c);

        if (index < 0 || index >= ALPHABET_SIZE) {
            return false;
        }

        if (nodes[curr].sub_tries[index] == EMPTY_CHILD) {
            return false;
        }
        curr = nodes[curr].sub_tries[index];
        if (curr >= pool->size) {
            fprintf(stderr, "is_word_in_trie: Trie node out of bounds\n");
            return false;
        }

    }

    if (token_type) {
        *token_type = nodes[curr].token_type;
    }

    return nodes[curr].end_of_word;
}
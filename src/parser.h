#ifndef sql_parser
#define sql_parse

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

struct ExprList *new_expr_list();
void init_expr_list(struct ExprList *expr_list);
void free_expr_list(struct ExprList *expr_list);
void write_to_expr_list(struct ExprList *expr_list, struct Expr *expr);

static struct ExprList *parse_expression_list(struct Parser *parser, struct Scanner *scanner);

struct SelectStatement *parse(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct Columns *parse_create(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);
struct CreateIndexStatement *parse_create_index(struct Parser *parser, const char *source, struct TriePool *reserved_words_pool);

#endif
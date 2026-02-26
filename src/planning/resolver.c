#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "../memory.h"
#include "resolver.h"
#include "../common.h"
#include "../pager.h"
#include "../parser.h"
#include "../data_parsing/page_parsing.h"
#include "../data_parsing/record_parsing.h"
#include "plan.h"
#include "../sql_utils.h"

#define INITIAL_HASH_MAP_CAPACITY (32)
#define INITIAL_HASH_MAP_LOAD_FACTOR (0.75)

DEFINE_TYPED_HASH_MAP(struct Column, size_t, ColumnToIndex, column_to_index)

// Turn column names into row indexes for each stage of the query

static bool get_is_first_col_rowid(const struct Column *col) {
    fprintf(stderr, "get_is_first_col_rowid: %.*s\n", (int)col->name.len, col->name.start);
    return col->name.len == 2 &&
           col->name.start[0] == 'i' &&
           col->name.start[1] == 'd';
}

static struct Columns *load_table_columns(struct Pager *pager, const char *table_name) {
    struct SchemaRecord *schema_record = get_schema_record_for_table(pager, table_name);
    struct Parser parser_create;
    // Empty pool, dont care about reserved words here
    struct TriePool *reserved_words_pool = init_reserved_words();

    return parse_create(&parser_create, schema_record->body.sql, reserved_words_pool);
}

static void add_table_to_hash_map(struct HashMap *hash_map, const struct Columns *columns, size_t index_offset) {
    assert(columns);
    for (size_t i = 0; i < columns->count; i++) {
        struct Column *column = &columns->data[i];
        size_t idx = i + index_offset;
        hash_map_column_to_index_set(hash_map, column, &idx);
    }
}

static struct HashMap *get_full_row_col_to_idx_hash_map(struct Resolver *resolver, struct Pager *pager, struct SelectStatement *stmt) {

    struct HashMap *column_to_index = hash_map_column_to_index_new(
        INITIAL_HASH_MAP_CAPACITY,
        INITIAL_HASH_MAP_LOAD_FACTOR,
        hash_column_ptr,
        equals_column_ptr
    );

    // Will iterate through tables when there are multiple
    struct Columns *columns = load_table_columns(pager, stmt->from_table);
    // @TODO: should this be elsewhere?
    print_unterminated_string_to_stderr(&columns->data[0].name);
    resolver->first_col_is_rowid = get_is_first_col_rowid(&columns->data[0]);
    add_table_to_hash_map(column_to_index, columns, 0);
    // vector_columns_free(columns);

    return column_to_index;
}

static struct HashMap *get_post_aggregation_row_col_to_idx_hash_map(struct SelectStatement *stmt) {
    if (!stmt) {
        return NULL;
    }

    struct HashMap *column_to_index = hash_map_new(
        INITIAL_HASH_MAP_CAPACITY,
        INITIAL_HASH_MAP_LOAD_FACTOR,
        sizeof(struct Column),
        sizeof(size_t),
        hash_column_ptr,
        equals_column_ptr
    );

    for (size_t i = 0; i < stmt->select_list->count; i++) {
        struct Expr expr = stmt->select_list->data[i];
        struct Column column = { .index = i, .name = expr.text };
        hash_map_column_to_index_set(column_to_index, &column, &i);
    }

    return column_to_index;
}

void set_index_for_expr_column(struct Resolver *resolver, struct ExprColumn *expr, enum PlanType type) {
    fprintf(stderr, "set_index_for_expr_column: called\n");
    print_expr_column_to_stderr(expr, 4);
    switch (type) {
        case PLAN_FILTER: {
            struct Column column = { .index = 0, .name = expr->name };
            size_t *idx = hash_map_column_to_index_get(resolver->full_row_col_to_idx, &column);
            expr->idx = *idx;
            break;
        }

        case PLAN_PROJECTION: {    
            struct Column column = { .index = 0, .name = expr->name };
            struct HashMap *hash_map = resolver->query_has_aggregates ? resolver->post_agg_row_col_to_idx : resolver->full_row_col_to_idx;
            size_t *idx = hash_map_column_to_index_get(hash_map, &column);
            expr->idx = *idx;
            break;
        }

        default:
            fprintf(stderr, "get_index_for_expr_column: unsupported plan type %d\n", type);
    }
}

void resolve_column_names(struct Resolver *resolver, struct ExprList *expr_list, enum PlanType type);
static void resolve_columns(struct Resolver *resolver, struct Expr *expr, enum PlanType type) {
    switch (expr->type) {
        case EXPR_INTEGER:
        case EXPR_STRING:
            break;

        case EXPR_BINARY:
            resolve_columns(resolver, expr->binary.left, type);
            resolve_columns(resolver, expr->binary.right, type);
            break;

        case EXPR_COLUMN:
            // @TODO: Get column index
            set_index_for_expr_column(resolver, &expr->column, type);
            break;

        case EXPR_FUNCTION:
            resolve_column_names(resolver, expr->function.args, type);
            break;

        case EXPR_UNARY:
            resolve_columns(resolver, expr->unary.right, type);
            break;

        case EXPR_STAR:
            break;

        default:
            fprintf(stderr, "Unknown ExprType %d\n", expr->type);
            exit(1);
    }
}

void resolve_column_names(struct Resolver *resolver, struct ExprList *expr_list, enum PlanType type) {
    if (!expr_list) {
        return;
    }

    for (size_t i = 0; i < expr_list->count; i++) {
        struct Expr *expr = &expr_list->data[i];
        fprintf(stderr, "Resolving\n");
        print_expression_to_stderr(expr, 4);
        resolve_columns(resolver, expr, type);
    }
}

struct Resolver *new_resolver(bool query_has_aggregates) {
    struct Resolver *resolver = malloc(sizeof(struct Resolver));
    if (!resolver) {
        fprintf(stderr, "resolve_names: failed to malloc *resolver.\n");
        exit(1);
    }

    resolver->full_row_col_to_idx       = NULL;
    resolver->post_agg_row_col_to_idx   = NULL;
    resolver->first_col_is_rowid        = false;
    resolver->query_has_aggregates      = query_has_aggregates;

    return resolver;
}

struct Resolver *resolver_init(struct Resolver *resolver, struct Pager *pager, struct SelectStatement *stmt) {

    resolver->full_row_col_to_idx = get_full_row_col_to_idx_hash_map(resolver, pager, stmt);
    resolver->post_agg_row_col_to_idx = get_post_aggregation_row_col_to_idx_hash_map(stmt);

    return resolver;
}

struct SizeTVec *get_projection_indexes(struct Resolver *resolver, struct SelectStatement *stmt) {
    struct HashMap *hash_map = resolver->query_has_aggregates ? resolver->post_agg_row_col_to_idx : resolver->full_row_col_to_idx;
    assert(hash_map != NULL);
    struct SizeTVec *indexes = vector_size_t_new();

    // @TODO: currently only handling column expr
    for (size_t i = 0; i < stmt->select_list->count; i++) {
        struct Expr *expr = &stmt->select_list->data[i];
        if (expr->type != EXPR_COLUMN) {
            continue;
        }

        struct Column column = { .index = 0, .name = expr->column.name };
        size_t *idx = hash_map_column_to_index_get(hash_map, &column);
        assert(idx != NULL);

        vector_size_t_push(indexes, *idx);
    }

    return indexes;
}
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "table_scan.h"
#include "../ast.h"
#include "../sql_utils.h"
#include "../data_parsing/page_parsing.h"
#include "../data_parsing/record_parsing.h"
#include "../tree_walker.h"
#include "../comparisons.h"
#include "plan.h"

#define COLUMNS_IN_EXPR_HASH_MAP_MIN_SIZE 8

static struct HashMap *get_hash_map_from_columns_array(struct Columns *columns) {
    struct HashMap *hash_map = hash_map_column_to_bool_new(
        columns->count,
        0.75,
        hash_column_ptr,
        equals_column_ptr
    );

    bool set_value = true;
    for (size_t i = 0; i < columns->count; i++) {
        const struct Column *column = &columns->data[i];
        print_unterminated_string_to_stderr(&column->name);
        hash_map_column_to_bool_set(hash_map, column, &set_value);
    }

    return hash_map;
}

static struct BinaryExprList *collect_index_predicates(struct IndexData *index_data, struct SelectStatement *stmt) {
    if (stmt->where_list == NULL) {
        fprintf(stderr, "where_list == NULL\n");
        return NULL;
    }

    struct BinaryExprList *expr_list = vector_binary_expr_list_new();

    struct HashMap *hash_map = get_hash_map_from_columns_array(index_data->columns);

    struct Expr *expr;
    for (size_t i = 0; i < stmt->where_list->count; i++) {
        expr = &stmt->where_list->data[i];
        
        if (expr->type != EXPR_BINARY) {
            continue;
        }

        struct HashMap *columns_hash_map = hash_map_column_to_bool_new(
            COLUMNS_IN_EXPR_HASH_MAP_MIN_SIZE,
            0.75,
            hash_column_ptr,
            equals_column_ptr
        );
        
        get_column_from_expression(expr, columns_hash_map);

        size_t columns_element_count;
        struct Column **columns = hash_map_column_to_bool_get_keys_alloc(columns_hash_map, &columns_element_count);
        struct Column *column;

        bool index_contains_all_columns = true;
        for (size_t j = 0; j < columns_element_count; j++) {
            column = columns[j];

            if (!hash_map_column_to_bool_contains(hash_map, column)) {
                index_contains_all_columns = false;
                break;
            }

        }

        if (index_contains_all_columns) {
            vector_binary_expr_list_push(expr_list, expr->binary);
        }

        free(columns);
        hash_map_column_to_bool_free(columns_hash_map);
        free(columns_hash_map);
    }

    hash_map_column_to_bool_free(hash_map);
    free(hash_map);

    return expr_list;
}

static struct IndexData *get_best_index(struct Pager *pager, struct SelectStatement *stmt) {
    fprintf(stderr, "new_get_best_index: called.\n");

    if (stmt->where_list == NULL) {
        fprintf(stderr, "where_list == NULL\n");
        return NULL;
    }

    struct HashMap *column_hash_map = get_columns_from_expression_list(stmt->where_list);
    struct IndexColumnsArray *index_array = get_all_indexes_for_table(pager, stmt->from_table);

    fprintf(stderr, "Found %zu indexes\n", index_array->count);

    // Pick the index with the most matching columns
    struct IndexData *best_index = malloc(sizeof(struct IndexData));
    if (!best_index) {
        fprintf(stderr, "new_get_best_index: failed to malloc *best_index.\n");
        exit(1);
    }

    best_index->columns     = NULL;
    best_index->predicates  = NULL;
    best_index->root_page   = 0;

    struct IndexColumns index_data;
    struct Column index_column;
    int matching_columns_max = 0;
    int matching_columns_cur = 0;

    // @TODO: should be using hash map/set
    // For each index, compare it in order to hash map of all binary predicate columns
    // Increment matching_columns_cur for every column it contains 
    // If the index has the most column matches it is the best index so far
    // Set matching_columns_max = matching_columns_cur
    for (size_t i = 0; i < index_array->count; i++) {
        matching_columns_cur = 0;
        index_data = index_array->data[i];

        for (size_t j = 0; j < index_data.columns->count; j++) {
            index_column = index_data.columns->data[j];

            if (!hash_map_column_to_bool_contains(column_hash_map, &index_column)) {
                break;
            }

            matching_columns_cur++;
        }

        if (matching_columns_cur > matching_columns_max) {
            // Index data doesnt have predicates
            best_index->columns     = index_data.columns;
            best_index->root_page   = index_data.root_page;
            matching_columns_max    = matching_columns_cur;
        }
    }

    hash_map_column_to_bool_free(column_hash_map);
    vector_index_columns_array_free(index_array);

    if (best_index->root_page == 0) {   
        fprintf(stderr, "Could not find suitable index.\n");
        free(best_index);
        return NULL;
    }

    fprintf(stderr, "Best index columns: \n");
    for (size_t i = 0; i < best_index->columns->count; i++) {
        index_column = best_index->columns->data[i];
        fprintf(stderr, "Col %zu: %.*s\n", i, (int)index_column.name.len, index_column.name.start);
    }

    // Fetch predicates using index columns
    best_index->predicates = collect_index_predicates(best_index, stmt);
    fprintf(stderr, "Best index predicates: \n");
    fprintf(stderr, "Count: %zu\n", best_index->predicates->count);
    print_binary_expr_list_to_stderr(best_index->predicates, 4);

    fprintf(stderr, "Returning best index\n");
    return best_index;
    

    // @TODO:
    // Identify all columns in where statment
    // Identify all matching indexes
    // Score columns based on where conditions
    // Select best index based on score
}

bool table_scan_next(struct TableScan *table_scan, struct Row *row) {
    // Decode all columns of row into struct Row
    // fprintf(stderr, "table_scan_next\n");
    return produce_row(table_scan->walker, row);
}

struct Plan *make_table_scan(struct Pager *pager, struct SelectStatement *stmt) {
    struct TableScan *table_scan = malloc(sizeof(struct TableScan));
    if (!table_scan) {
        fprintf(stderr, "make_table_scan: *table_scan malloc failed\n");
        exit(1);
    }

    struct SchemaRecord *schema_record = get_schema_record_for_table(pager, stmt->from_table);
    
    struct PageHeader page_header;
    read_page_header(pager, &page_header, schema_record->body.root_page);
    

    // Find any indexes for our table
    // Check if any predicate matches that index
    struct IndexData *index = get_best_index(pager, stmt);

    memset(table_scan, 0, sizeof *table_scan);
    table_scan->base.type       = PLAN_TABLE_SCAN;
    table_scan->row_cursor      = 0;
    table_scan->root_page       = schema_record->body.root_page;
    table_scan->table_name      = stmt->from_table;
    table_scan->index           = index;

    table_scan->walker = new_tree_walker(pager, table_scan->root_page, table_scan->index);

    return &table_scan->base;
}
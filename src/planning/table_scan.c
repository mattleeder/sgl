#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../ast.h"
#include "../sql_utils.h"
#include "../parser.h"
#include "../data_parsing/page_parsing.h"
#include "../data_parsing/record_parsing.h"
#include "../tree_walker.h"
#include "../comparisons.h"
#include "plan.h"

#define COLUMNS_IN_EXPR_HASH_MAP_MIN_SIZE 8

struct TableScan {
    struct Plan         base;
    size_t              row_cursor;
    uint32_t            root_page;
    bool                first_col_is_row_id;
    char                *table_name;
    struct Columns      *columns;
    struct TreeWalker   *walker;
    struct Index        *index;
};

static struct ColumnToBoolHashMap *get_hash_map_from_columns_array(struct Columns *columns) {
    struct ColumnToBoolHashMap *hash_map = malloc(sizeof(struct ColumnToBoolHashMap));
    if (!hash_map) {
        fprintf(stderr, "get_hash_map_from_columns_array: failed to malloc *hash_map.\n");
        exit(1);
    }

    fprintf(stderr, "Col count %zu\n", columns->count);
    hash_map_column_to_bool_init(hash_map, columns->count, 0.75);
    fprintf(stderr, "get_hash_map_from_columns_array: hash_map_column_to_bool_init successful\n");

    struct Column column;
    for (int i = 0; i < columns->count; i++) {
        column = columns->data[i];
        fprintf(stderr, "%.*s\n", column.name_length, column.name_start);
        hash_map_column_to_bool_set(hash_map, column, true);
    }

    return hash_map;
}

static struct BinaryExprList *collect_index_predicates(struct IndexData *index_data, struct SelectStatement *stmt) {
    if (stmt->where_list == NULL) {
        fprintf(stderr, "where_list == NULL\n");
        return NULL;
    }

    struct BinaryExprList *expr_list = malloc(sizeof(struct BinaryExprList));

    if (!expr_list) {
        fprintf(stderr, "collect_index_predicates: failed to malloc *expr_list.\n");
        exit(1);
    }

    init_binary_expr_list(expr_list);

    struct ColumnToBoolHashMap *hash_map = get_hash_map_from_columns_array(index_data->columns);

    struct Expr *expr;
    for (int i = 0; i < stmt->where_list->count; i++) {
        expr = &stmt->where_list->data[i];
        
        if (expr->type != EXPR_BINARY) {
            continue;
        }

        struct ColumnToBoolHashMap *columns_hash_map = malloc(sizeof(struct ColumnToBoolHashMap));
        if (!columns_hash_map) {
            fprintf(stderr, "collect_index_predicates: failed to malloc *columns_hash_map.\n");
            exit(1);
        }
        
        
        hash_map_column_to_bool_init(columns_hash_map, COLUMNS_IN_EXPR_HASH_MAP_MIN_SIZE, 0.75);
        get_column_from_expression(expr, columns_hash_map);

        size_t columns_element_count;
        struct Column *columns = hash_map_column_to_bool_keys_to_array(columns_hash_map, &columns_element_count);
        struct Column column;

        bool index_contains_all_columns = true;
        for (int j = 0; j < columns_element_count; j++) {
            column = columns[j];

            if (!hash_map_column_to_bool_contains(hash_map, column)) {
                index_contains_all_columns = false;
                break;
            }

        }

        if (index_contains_all_columns) {
            fprintf(stderr, "Pushing\n");
            push_binary_expr_list(expr_list, expr->binary);
            fprintf(stderr, "Count is now %zu\n", expr_list->count);
        }


        free(columns);
        hash_map_column_to_bool_free(columns_hash_map);
        free(columns_hash_map);
    }

    hash_map_column_to_bool_free(hash_map);
    free(hash_map);

    return expr_list;
}

static struct IndexData *new_get_best_index(struct Pager *pager, struct SelectStatement *stmt) {
    fprintf(stderr, "new_get_best_index: called.\n");

    if (stmt->where_list == NULL) {
        fprintf(stderr, "where_list == NULL\n");
        return NULL;
    }

    struct ColumnToBoolHashMap *column_hash_map = get_columns_from_expression_list(stmt->where_list);
    struct IndexColumnsArray *index_array = get_all_indexes_for_table(pager, stmt->from_table);

    fprintf(stderr, "Found %d indexes\n", index_array->count);

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
    fprintf(stderr, "Searching\n");
    for (int i = 0; i < index_array->count; i++) {
        fprintf(stderr, "i: %d\n", i);
        matching_columns_cur = 0;
        index_data = index_array->data[i];

        for (int j = 0; j < index_data.columns->count; j++) {
            index_column = index_data.columns->data[j];

            if (!hash_map_column_to_bool_contains(column_hash_map, index_column)) {
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
    free_index_columns_array(index_array);

    if (best_index->root_page == 0) {   
        fprintf(stderr, "Could not find suitable index.\n");
        free(best_index);
        return NULL;
    }

    fprintf(stderr, "Best index columns: \n");
    for (int i = 0; i < best_index->columns->count; i++) {
        index_column = best_index->columns->data[i];
        fprintf(stderr, "Col %d: %.*s\n", i, index_column.name_length, index_column.name_start);
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

static struct Index *get_best_index(struct Pager *pager, struct SelectStatement *stmt) {
    // Find all indexes we can partially or fully use
    // Decide which is the best

    fprintf(stderr, "searching for best index to use\n");
    struct Expr *expr;
    if (stmt->where_list == NULL) {
        return NULL;
    }

    uint32_t index_root_page;
    struct Index *index = NULL;

    for (int i = 0; i < stmt->where_list->count; i++) {
        expr = &stmt->where_list->data[i];
        if (expr->type != EXPR_BINARY) {
            fprintf(stderr, "Currently only support index searching for binary predicates.\n");
            continue;
        }
        
        fprintf(stderr, "Checking index for \n");
        print_expression_to_stderr(expr, 4);
        
        // @TODO: currently only getting single index
        if (expr->binary.left->type == EXPR_COLUMN) {
            index_root_page = get_root_page_of_first_matching_index(pager, stmt->from_table, expr->binary.left->column.start, expr->binary.left->column.len);
            fprintf(stderr, "Left index root page: %d\n", index_root_page);
            
            
            if (index_root_page != 0) {
                index = malloc(sizeof(struct Index));
                if (!index) {
                    fprintf(stderr, "make_table_scan: left *index malloc failed\n");
                    exit(1);
                }
                
                index->column_name_start    = expr->binary.left->column.start;
                index->column_name_length   = expr->binary.left->column.len;
                index->predicate            = &expr->binary;
                index->root_page            = index_root_page;
                break;
            }
        }
        
        if (expr->binary.right->type == EXPR_COLUMN) {
            index_root_page = get_root_page_of_first_matching_index(pager, stmt->from_table, expr->binary.right->column.start, expr->binary.right->column.len);
            fprintf(stderr, "Right index root page: %d\n", index_root_page);
            
            if (index_root_page != 0) {
                index = malloc(sizeof(struct Index));
                if (!index) {
                    fprintf(stderr, "make_table_scan: right *index malloc failed\n");
                    exit(1);
                }
                
                index->column_name_start    = expr->binary.right->column.start;
                index->column_name_length   = expr->binary.right->column.len;
                index->predicate            = &expr->binary;
                index->root_page            = index_root_page;
                break;
            }
        }   
    }

    if (index != NULL) {
        fprintf(stderr, "Found index on column: %*s\n", index->column_name_length, index->column_name_start);
        fprintf(stderr, "Index root page at: %d\n", index->root_page);
        fprintf(stderr, "Predicate: \n");
        print_expression_to_stderr(expr, 4);
    } else {
        fprintf(stderr, "Could not find suitable index.\n");
    }

    return index;
}


static bool table_scan_next(struct Pager *pager, struct TableScan *table_scan, struct Row *row) {
    // Decode all columns of row into struct Row
    // fprintf(stderr, "table_scan_next\n");
    return produce_row(table_scan->walker, row);
}

static struct Plan *make_table_scan(struct Pager *pager, struct SelectStatement *stmt) {
    fprintf(stderr, "make_table_scan\n");
    struct TableScan *table_scan = malloc(sizeof(struct TableScan));
    if (!table_scan) {
        fprintf(stderr, "make_table_scan: *table_scan malloc failed\n");
        exit(1);
    }

    struct SchemaRecord *schema_record = get_schema_record_for_table(pager, stmt->from_table);
    
    struct PageHeader page_header;
    read_page_header(pager, &page_header, schema_record->body.root_page);

    struct Parser parser_create;
    // Empty pool, dont care about reserved words here
    struct TriePool *reserved_words_pool = init_reserved_words();
    struct Columns *columns = parse_create(&parser_create, schema_record->body.sql, reserved_words_pool);

    // Find any indexes for our table
    // Check if any predicate matches that index
    struct Index *index = get_best_index(pager, stmt);
    new_get_best_index(pager, stmt);

    memset(table_scan, 0, sizeof *table_scan);
    table_scan->base.type       = PLAN_TABLE_SCAN;
    table_scan->row_cursor      = 0;
    table_scan->root_page       = schema_record->body.root_page;
    table_scan->table_name      = stmt->from_table;
    table_scan->columns         = columns;
    table_scan->index           = index;

    for (int i = 0; i < table_scan->columns->count; i++) {
        struct Column column = table_scan->columns->data[i];
        fprintf(stderr, "make_table_scan: Column %d %.*s\n", i, (int)column.name_length, column.name_start);
    }

    // @TODO: this is not the appropriate check, needs to be ID INTEGER or something
    struct Column column = table_scan->columns->data[0];
    table_scan->first_col_is_row_id = strncmp("id", column.name_start, 2) == 0;

    table_scan->walker = new_tree_walker(pager, table_scan->root_page, table_scan->first_col_is_row_id, table_scan->index);

    // fprintf(stderr, "There are %d rows\n", table_scan->table_scan.num_rows);
    fprintf(stderr, "make_table_scan: return\n");
    return &table_scan->base;
}
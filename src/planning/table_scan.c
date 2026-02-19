#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "../ast.h"
#include "../sql_utils.h"
#include "../parser.h"
#include "../data_parsing/page_parsing.h"
#include "../data_parsing/record_parsing.h"
#include "../tree_walker.h"
#include "plan.h"

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

static void new_get_best_index(struct SelectStatement *stmt) {
    if (stmt->where_list == NULL) {
        return NULL;
    }

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
        expr = stmt->where_list->list[i];
        if (expr->type != EXPR_BINARY) {
            fprintf(stderr, "Currently only support index searching for binary predicates.\n");
            continue;
        }
        
        fprintf(stderr, "Checking index for \n");
        print_expression_to_stderr(expr, 4);
        
        // @TODO: currently only getting single index
        if (expr->binary.left->type == EXPR_COLUMN) {
            index_root_page = get_root_page_of_first_matching_index(pager, stmt->from_table, expr->binary.left->column.name);
            fprintf(stderr, "Left index root page: %d\n", index_root_page);
            
            
            if (index_root_page != 0) {
                index = malloc(sizeof(struct Index));
                if (!index) {
                    fprintf(stderr, "make_table_scan: left *index malloc failed\n");
                    exit(1);
                }
                
                index->column_name   = expr->binary.left->column.name;
                index->predicate     = &expr->binary;
                index->root_page     = index_root_page;
                break;
            }
        }
        
        if (expr->binary.right->type == EXPR_COLUMN) {
            index_root_page = get_root_page_of_first_matching_index(pager, stmt->from_table, expr->binary.left->column.name);
            fprintf(stderr, "Right index root page: %d\n", index_root_page);
            
            if (index_root_page != 0) {
                index = malloc(sizeof(struct Index));
                if (!index) {
                    fprintf(stderr, "make_table_scan: right *index malloc failed\n");
                    exit(1);
                }
                
                index->column_name   = expr->binary.right->column.name;
                index->predicate     = &expr->binary;
                index->root_page     = index_root_page;
                break;
            }
        }   
    }

    if (index != NULL) {
        fprintf(stderr, "Found index on column: %s\n", index->column_name);
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

    memset(table_scan, 0, sizeof *table_scan);
    table_scan->base.type       = PLAN_TABLE_SCAN;
    table_scan->row_cursor      = 0;
    table_scan->root_page       = schema_record->body.root_page;
    table_scan->table_name      = stmt->from_table;
    table_scan->columns         = columns;
    table_scan->index           = index;

    for (int i = 0; i < table_scan->columns->count; i++) {
        char *name = table_scan->columns->names[i];
        size_t len = table_scan->columns->name_lengths[i];
        fprintf(stderr, "Column %d %.*s\n", i, (int)len, name);
    }

    // @TODO: this is not the appropriate check, needs to be ID INTEGER or something
    char *name = table_scan->columns->names[0];
    size_t len = table_scan->columns->name_lengths[0];
    table_scan->first_col_is_row_id = strncmp("id", name, len) == 0;

    table_scan->walker = new_tree_walker(pager, table_scan->root_page, table_scan->first_col_is_row_id, table_scan->index);

    // fprintf(stderr, "There are %d rows\n", table_scan->table_scan.num_rows);
    fprintf(stderr, "make_table_scan: return\n");
    return &table_scan->base;
}
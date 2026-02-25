#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "memory.h"
#include "data_parsing/page_parsing.h"
#include "data_parsing/cell_parsing.h"
#include "data_parsing/record_parsing.h"
#include "data_parsing/row_parsing.h"
#include "sql_utils.h"
#include "tree_walker.h"
#include "comparisons.h"
#include "planning/plan.h"


// Given root page for index
// Walk the tree and binary search interior trees to find first child
// Once we are at a leaf node, binary search to find first matching key
// We can then iterate over they keys, producing row ids until
// Our constraint is no longer met

// Algorithm
// 1. If we have index, initialise walker with root page of index, if not skip to step
// 2. Read the page header
// 3. Read the cell pointer array
// 4. If the page is an interior index page continue to step 5. otherwise skip to step 7.
// 5. Binary search to find first matching key
// 6. Visit left child
// 7. If the page is a leaf table page, binary search to find first matching key

// Given root page for table
// Walk the tree and produce the rows in order

// 1. Initialise walker with root page of a table
// 2. Read the page header
// 3. Read the cell pointer array
// 4. If the page is an interior table page continue to step 5. otherwise skip to step 7.
// 5. Iterate over each cell in order, at each cell visit that page and go back to step 2.
// 6. Read right most pointer and visit that page
// 7. If the page is a leaf table page, iterate over each cell in order and produce a row

void init_sub_walker_list(struct SubWalkerList *tree_walker_list) {
    tree_walker_list->count          = 0;
    tree_walker_list->capacity       = 0;
    tree_walker_list->list           = NULL;
}

void free_sub_walker(struct SubWalker *walker) {
    free(walker->page_header);
    free(walker->cell_pointer_array);
    free(walker->cell);
    walker->page                = 0;
    walker->pager               = NULL;
    walker->page_header         = NULL;
    walker->cell_pointer_array  = NULL;
}

void free_sub_walker_list(struct SubWalkerList *sub_walker_list) {
    for (int i = 0; i < sub_walker_list->count; i++) {
        free_sub_walker(sub_walker_list->list[i]);
    }
    FREE_ARRAY(struct SubWalker *, sub_walker_list->list, sub_walker_list->capacity);
    init_sub_walker_list(sub_walker_list);
}

void free_tree_walker(struct TreeWalker *walker) {
    free_sub_walker_list(walker->table_list);
    free_sub_walker_list(walker->index_list);
    free(walker->table_list);
    free(walker->index_list);
    free(walker);
}

void add_sub_walker(struct SubWalkerList *sub_walker_list, struct SubWalker *walker) {
    if (sub_walker_list->capacity < sub_walker_list->count + 1) {
        int old_capacity = sub_walker_list->capacity;
        sub_walker_list->capacity       = grow_capacity(old_capacity);
        sub_walker_list->list           = GROW_ARRAY(struct SubWalker *, sub_walker_list->list, old_capacity, sub_walker_list->capacity);
    }

    sub_walker_list->list[sub_walker_list->count]        = walker;
    sub_walker_list->count++;
}

void remove_last_walker(struct SubWalkerList *sub_walker_list) {
    if (sub_walker_list->count == 0) {
        fprintf(stderr, "SubWalkerList already empty.\n");
        exit(1);
    }

    free_sub_walker(sub_walker_list->list[sub_walker_list->count - 1]);
    sub_walker_list->count--;
}

// Step 1.
struct SubWalker *new_sub_walker(struct Pager *pager, uint32_t page, struct Index *index) {
    struct SubWalker *walker = malloc(sizeof(struct SubWalker));
    
    if (!walker) {
        fprintf(stderr, "new_sub_walker: *walker malloc failed\n");
        exit(1);
    }

    walker->page                 = page;
    walker->pager                = pager;
    walker->page_header          = malloc(sizeof(struct PageHeader));
    walker->cell_pointer_array   = NULL;
    walker->current_index        = 0;
    walker->cell                 = malloc(sizeof(struct Cell));
    walker->step                 = NULL;
    walker->index                = index;

    if (!walker->page_header) {
        fprintf(stderr, "new_sub_walker: walker->page_header failed\n");
        exit(1);
    }

    if (!walker->cell) {
        fprintf(stderr, "new_sub_walker: walker->page_header failed\n");
        exit(1);
    }

    return walker;
}

void take_step(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *rowid_valid) {
    // fprintf(stderr, "take step\n");
    if (walker == NULL) {
        fprintf(stderr, "Walker is NULL.\n");
        exit(1);
    }

    if (walker->step == NULL) {
        fprintf(stderr, "Walker step function is undefined.\n");
        exit(1);
    }

    if (list == NULL) {
        fprintf(stderr, "SubWalkerList is NULL.\n");
        exit(1);
    }

    if (row == NULL) {
        fprintf(stderr, "Row is NULL.\n");
        exit(1);
    }

    if (next_rowid == NULL) {
        fprintf(stderr, "Next rowid is NULL.\n");
        exit(1);
    }

    if (rowid_valid == NULL) {
        fprintf(stderr, "rowid_valid is NULL.\n");
        exit(1);
    }

    walker->step(walker, list, row, next_rowid, rowid_valid);
}


void interior_table_step(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *row_valid) {
    // fprintf(stderr, "interior_table_step\n");

    if (walker->page_header->number_of_cells <= 0) {
        fprintf(stderr, "interior_table_step: Expected at least 1 cell\n");
        exit(1);
    }

    // fprintf(stderr, "There are %hu cells\n", walker->page_header->number_of_cells);

    int lo = walker->current_index;
    int hi = walker->page_header->number_of_cells - 1;
    int mid = lo + (hi - lo) / 2;
    int result = -1;
    uint16_t cell_offset;

    while (lo <= hi) {
        mid = lo + (hi - lo) / 2;
               
        cell_offset = walker->cell_pointer_array[mid];
        read_cell(walker->pager, walker->page_header, walker->cell, cell_offset);
        // fprintf(stderr, "lo: %d, hi: %d, mid: %d, integer key:%lld\n", lo, hi, mid, walker->cell->data.table_interior_cell.integer_key);

        if (walker->cell->data.table_interior_cell.integer_key >= *next_rowid) {
            // Record
            result = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    // If integer key < rowid search left child else search right
    // if (*next_rowid >= walker->cell->data.table_interior_cell.integer_key) {
    //     result++;
    //     cell_offset = walker->cell_pointer_array[result];
    //     read_cell(walker->pager, walker->page, walker->page_header, walker->cell, cell_offset);
    // }
    // fprintf(stderr, "Target: %d, integer key: %d, result: %d, lo: %d, hi: %d\n", *next_rowid, walker->cell->data.table_interior_cell.integer_key, result, lo, hi);
    // if (result == -1) {
    //     remove_last_walker(list);
    //     return;
    // }

    walker->current_index = result + 1;

    struct SubWalker *new_walker;
    uint32_t next_child;
    if ( result >= 0 && result < walker->page_header->number_of_cells) {
        // A left child
        next_child = walker->cell_pointer_array[result];
        read_cell(walker->pager, walker->page_header, walker->cell, next_child);
        new_walker = new_sub_walker(walker->pager, walker->cell->data.table_interior_cell.left_child_pointer, walker->index);
        // fprintf(stderr, "Begin walk from left child\n");

    } else {
        // Right most child
        next_child = walker->page_header->right_most_pointer;
        new_walker = new_sub_walker(walker->pager, next_child, walker->index);
        remove_last_walker(list);
        // fprintf(stderr, "Begin walk from right most child\n");

    }

    // @TODO: dont keep initialsing new walkers
    begin_walk(new_walker);
    add_sub_walker(list, new_walker);
    return;
}

void leaf_table_step(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *row_valid) {
    // fprintf(stderr, "leaf_table_step\n");

    if (walker->page_header->number_of_cells <= 0) {
        fprintf(stderr, "leaf_table_step: Expected at least 1 cell\n");
        exit(1);
    }

    int lo = walker->current_index;
    int hi = walker->page_header->number_of_cells - 1;
    int mid = lo + (hi - lo) / 2;


    uint16_t cell_offset;
    while (lo <= hi) {
        mid = lo + (hi - lo) / 2;
                
        cell_offset = walker->cell_pointer_array[mid];
        read_cell(walker->pager, walker->page_header, walker->cell, cell_offset);
        // fprintf(stderr, "lo: %d, hi: %d, mid: %d, rowid: %lld\n", lo, hi, mid, walker->cell->data.table_leaf_cell.row_id);

        if (walker->cell->data.table_leaf_cell.row_id == *next_rowid) {
            // Return this row
            break;
        } else if (walker->cell->data.table_leaf_cell.row_id < *next_rowid) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (walker->cell->data.table_leaf_cell.row_id != *next_rowid) {
        // Row not here, call next walker
        remove_last_walker(list);
        return;
    }

    *row_valid = true;

    walker->current_index = mid + 1;

    // Step 7.
    read_cell_offset_into_row(walker->pager, row, walker->page_header, cell_offset);

    if (row->column_count == 0) {
        fprintf(stderr, "leaf_table_step: Row has no values.\n");
        exit(1);
    }

    // Remove walker if all cells exhausted
    if (walker->current_index >= walker->page_header->number_of_cells) {
        remove_last_walker(list);
    }
}

void interior_index_step(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *rowid_valid) {
    // fprintf(stderr, "interior_index_step, page: %d\n", walker->page_header->page_number);
    struct Row index_row;
    struct Value predicate_value = get_predicate_value(&walker->index->predicates[0]);
    
    // Binary search to find first key where predicate is met
    uint16_t lo = walker->current_index;
    uint16_t hi = walker->page_header->number_of_cells - 1;
    uint16_t mid = lo + (hi - lo) / 2;
    
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        // fprintf(stderr, "lo: %d, hi: %d, mid: %d\n", lo, hi, mid);
        read_cell_offset_into_row(walker->pager, &index_row, walker->page_header, walker->cell_pointer_array[mid]);
        
        if (index_row.column_count < 1) {
            fprintf(stderr, "interior_index_step: Expected row to have at least 1 column\n");
            exit(1);
        }

        // Evaluate record body against given index
        if (compare_index_predicate(&walker->index->predicates[0], &index_row.values[0], &predicate_value)) {
            hi = mid;
        } else {
            lo =  mid + 1;
        }
    }

    // lo should give last false index
    // hi should give first true index
    // so we must check left child of hi
    walker->current_index = mid + 1;
    uint32_t pointer = walker->page_header->right_most_pointer;
    bool should_free = true;

    struct SubWalker *new_walker;
    if (mid < walker->page_header->number_of_cells) {
        pointer = walker->cell_pointer_array[mid];
        should_free = false;
        read_cell(walker->pager, walker->page_header, walker->cell, walker->cell_pointer_array[mid]);
        uint32_t next_root_page = walker->cell->data.index_interior_cell.left_child_pointer;
        new_walker = new_sub_walker(walker->pager, next_root_page, walker->index);
    } else {
        new_walker = new_sub_walker(walker->pager, pointer, walker->index);
    }

    if (should_free) remove_last_walker(list);
   
    add_sub_walker(list, new_walker);
    begin_walk(new_walker);
}

void leaf_index_step(struct SubWalker *walker, struct SubWalkerList *list, struct Row *row, uint64_t *next_rowid, bool *rowid_valid) {
    // fprintf(stderr, "leaf_index_step\n");
    struct Row index_row;
    struct Value predicate_value = get_predicate_value(&walker->index->predicates[0]);
    
    // Binary search to find first key where predicate is met
    uint16_t lo = walker->current_index;
    uint16_t hi = walker->page_header->number_of_cells - 1;
    uint16_t mid = lo + (hi - lo) / 2;

    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        // fprintf(stderr, "lo: %d, hi: %d, mid: %d\n", lo, hi, mid);
        read_cell_offset_into_row(walker->pager, &index_row, walker->page_header, walker->cell_pointer_array[mid]);

        if (index_row.column_count < 1) {
            fprintf(stderr, "leaf_index_step: Expected row to have at least 1 column but has %lld\n", row->column_count);
            exit(1);
        }
        
        // Evaluate record body against given index
        if (compare_index_predicate(&walker->index->predicates[0], &index_row.values[0], &predicate_value)) {
            hi = mid;
        } else {
            lo =  mid + 1;
        }
    }

    read_cell_offset_into_row(walker->pager, &index_row, walker->page_header, walker->cell_pointer_array[hi]);
    // print_row_to_stderr(&index_row);
    // @TODO: only workds for equals
    if (index_row.column_count < 1) {
        fprintf(stderr, "leaf_index_step post loop: Expected row to have at least 1 column\n");
        exit(1);
    }
    if (compare_values(&predicate_value, &index_row.values[0]) != 0) {
        // fprintf(stderr, "Row does not match predicate, keep searching\n");
        remove_last_walker(list);
        return;
    }

    walker->current_index = hi + 1;

    struct Value row_value = index_row.values[index_row.column_count - 1];
    if (row_value.type != VALUE_INT) {
        fprintf(stderr, "leaf_index_step: rowid value is %d not int.\n", row_value.type);
        // print_row_to_stderr(&index_row);
        exit(1);
    }

    // fprintf(stderr, "Writing %lld to next_rowid\n", row_value.int_value.value);
    *next_rowid = row_value.int_value.value;
    *rowid_valid = true;
}

void begin_walk(struct SubWalker *walker) {

    if (walker->page_header == NULL) {
        fprintf(stderr, "page_header is NULL.\n");
        exit(1);
    }

    if (walker->page > walker->pager->page_count) {
        fprintf(stderr, "page %d exceeds max %d.\n", walker->page, walker->pager->page_count);
        exit(1);
    }
    
    // Step 2.
    read_page_header(walker->pager, walker->page_header, walker->page);
    // fprintf(stderr, "Walker read page header: %d\n", walker->page_header->page_number);

    if (walker->page_header->number_of_cells <= 0) {
        fprintf(stderr, "interior_table_step: Expected at least 1 cell\n");
        exit(1);
    }

    // fprintf(stderr, "begin_walk: Number of cells: %d\n", walker->page_header->number_of_cells);

    // Step 3.
    walker->cell_pointer_array = read_cell_pointer_array(walker->pager, walker->page_header);

    if (walker->cell_pointer_array == NULL) {
        fprintf(stderr, "cell_pointer_array is NULL.\n");
        exit(1);
    }


    switch (walker->page_header->page_type) {
        
        case PAGE_INTERIOR_TABLE:
            walker->step = interior_table_step;
            break;

        case PAGE_LEAF_TABLE:
            walker->step = leaf_table_step;
            break;

        case PAGE_INTERIOR_INDEX:
            walker->step = interior_index_step;
            break;

        case PAGE_LEAF_INDEX:
            walker->step = leaf_index_step;
            break;

        default:
            fprintf(stderr, "Walker encountered unknown page type: %d.\n", walker->page_header->page_type);
            exit(1);
    }
}

struct TreeWalker *new_tree_walker(struct Pager *pager, uint32_t root_page, struct IndexData *index) {
    struct TreeWalker *walker = malloc(sizeof(struct TreeWalker));
    if (!walker) {
        fprintf(stderr, "new_tree_walker: *walker malloc failed\n");
        exit(1);
    }

    struct SubWalkerList *table_list = malloc(sizeof(struct SubWalkerList));
    if (!table_list) {
        fprintf(stderr, "new_tree_walker: *table_list malloc failed\n");
        exit(1);
    }

    
    init_sub_walker_list(table_list);

    walker->type                = index == NULL ? WALKER_FULL_SCAN : WALKER_INDEX_SCAN;
    walker->pager               = pager;
    walker->root_page           = root_page;
    walker->table_list          = table_list;
    walker->index               = index;
    walker->index_list          = NULL;
    walker->current_rowid       = 1;

    if (walker->type == WALKER_INDEX_SCAN) {
        fprintf(stderr, "Index root page is %d\n", index->root_page);

        struct SubWalkerList *index_list = malloc(sizeof(struct SubWalkerList));
        if (!index_list) {
            fprintf(stderr, "new_tree_walker: *index_list malloc failed\n");
            exit(1);
        }

        init_sub_walker_list(index_list);
        walker->index_list = index_list;

        struct SubWalker *index_sub_walker = new_sub_walker(pager, index->root_page, walker->index);
        add_sub_walker(index_list, index_sub_walker);
        fprintf(stderr, "Sub Walker page: %d\n", index_sub_walker->page);
        begin_walk(index_sub_walker);
    }
    
    uint32_t sub_walker_root_page = walker->root_page;
    struct SubWalker *sub_walker = new_sub_walker(pager, sub_walker_root_page, walker->index);
    add_sub_walker(table_list, sub_walker);
    begin_walk(sub_walker);

    return walker;
}

bool produce_rowid(struct TreeWalker *walker, struct Row *row, uint64_t *next_rowid) {
    // fprintf(stderr, "produce_rowid\n");
    bool rowid_valid = false;
    while (walker->index_list->count > 0) {
        struct SubWalker *last_sub_walker = walker->index_list->list[walker->index_list->count - 1];
        take_step(last_sub_walker, walker->index_list, row, next_rowid, &rowid_valid);
        if (rowid_valid) {
            break;
        }
    }
    return rowid_valid;
}

bool produce_row(struct TreeWalker *walker, struct Row *row) {
    uint64_t next_rowid = walker->current_rowid;
    bool rowid_valid = false;

    // fprintf(stderr, "Find rowid\n");
    if (walker->index_list != NULL) {
        if (walker->index_list->count == 0) {
            return false;
        }
        
        if (!produce_rowid(walker, row, &next_rowid)) {
            fprintf(stderr, "Could not find rowid, assumed exhausted.\n");
            return false;
        }
    }

    // fprintf(stderr, "Next rowid: %lld, current rowid: %lld\n", next_rowid, walker->current_rowid);
    if (next_rowid < walker->current_rowid) {
        fprintf(stderr, "Next rowid: %lld is less than current rowid %lld.\n", next_rowid, walker->current_rowid);
        return false;
    }
    walker->current_rowid = next_rowid;

    if (walker->table_list->count == 0) {
        fprintf(stderr, "No table walkers\n");
        return false;
    }

    // fprintf(stderr, "Search in table tree for rowid: %lld\n", next_rowid);
    bool row_valid = false;
    while (walker->table_list->count > 0) {
        struct SubWalker *last_sub_walker = walker->table_list->list[walker->table_list->count - 1];
        take_step(last_sub_walker, walker->table_list, row, &next_rowid, &row_valid);
        if (row_valid) {
            break;
        }
    }

    if (!row_valid) {
        return false;
    }

    // fprintf(stderr, "\n\n Produced row:\n\n");
    // print_row_to_stderr(row);
    // fprintf(stderr, "\n\n");
    walker->current_rowid++;
    return true;
}
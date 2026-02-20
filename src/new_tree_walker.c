// #include <stdint.h>
// #include <stdlib.h>
// #include <stdbool.h>
// #include <assert.h>

// #include "memory.h"
// #include "sql_utils.h"
// #include "comparisons.h"


// // Expose 2 functions, new_tree_walker() and produce_row()
// // new_tree_walker()
// //  decides strategy based on inputs
// //  How do we decide?
// //  If struct IndexData != NULL then index scan, else full table scan
// //  
// //  
// // produce_row()
// //  returns bool 
// //  writes row data into given object
// //  if no row return false else return true

// // Two kinds of search
// // Index Search
// //  Scan through index tables to find the next rowid
// //  Find the rowid in data tables
// // Full Scan
// //  Return all rowid in order

// enum WalkStrategy {
//     WALK_FULL_SCAN,
//     WALK_INDEX_SCAN
// };

// // Only handles equalities
// struct IndexConstraints {
//     const struct ExprBinary * const     *keys;    // Binary expressions in the order columns appear in index
//     size_t                          key_count;
//     uint32_t                        root_page;
// };

// struct StepData {
//     struct PageHeader   *page_header;
//     uint16_t            *cell_pointer_array;
//     uint32_t            search_start;
// };

// struct StepsList {
//     size_t              count;
//     size_t              capacity;
//     struct StepData     *list;
// };

// struct BTreeCursor {
//     struct StepsList    steps;
//     struct Pager        *pager; // Borrowed
//     bool                scan_finished;
//     uint32_t            root_page;
// };

// struct TreeWalker {
//     struct BTreeCursor          table_cursor;
//     struct BTreeCursor          index_cursor;
//     struct IndexConstraints     *index_constraints;
//     enum WalkStrategy           strategy;
// };

// struct StepData new_step(struct Pager *pager, uint32_t page_number) {
//     struct StepData new_step_data;
//     new_step_data.page_header = malloc(sizeof(struct PageHeader));
//     if (!new_step_data.page_header) {
//         fprintf(stderr, "new_step: new_step_data.page_header malloc failed.\n");
//         exit(1);
//     }

//     read_page_header(pager, new_step_data.page_header, page_number);
//     new_step_data.cell_pointer_array = read_cell_pointer_array(pager, new_step_data.page_header);

//     return new_step_data;
// }

// void free_step_data(struct StepData *step_data) {
//     free(step_data->page_header);
//     free(step_data->cell_pointer_array);

//     step_data->page_header          = NULL;
//     step_data->cell_pointer_array   = NULL;
// }

// void init_steps_list(struct StepsList *steps_list) {
//     steps_list->count          = 0;
//     steps_list->capacity       = 0;
//     steps_list->list           = NULL;
// }

// void free_steps_list(struct StepsList *steps_list) {
//     for (int i = 0; i < steps_list->count; i++) {
//         free_step_data(&steps_list->list[i]);
//     }
//     FREE_ARRAY(struct StepsList, steps_list->list, steps_list->capacity);
//     init_sub_walker_list(steps_list);
// }

// void add_step(struct StepsList *steps_list, struct StepData step) {
//     if (steps_list->capacity < steps_list->count + 1) {
//         int old_capacity = steps_list->capacity;
//         steps_list->capacity       = grow_capacity(old_capacity);
//         steps_list->list           = GROW_ARRAY(struct StepData, steps_list->list, old_capacity, steps_list->capacity);
//     }

//     steps_list->list[steps_list->count] = step;
//     steps_list->count++;
// }

// void remove_last_step(struct StepsList *steps_list) {
//     if (steps_list->count == 0) {
//         fprintf(stderr, "StepsList already empty.\n");
//         exit(1);
//     }

//     steps_list->count--;
// }

// struct StepData *get_last_step(struct BTreeCursor *cursor) {
//     if (cursor->steps.count < 1) {
//         return NULL;
//     }

//     return &cursor->steps.list[cursor->steps.count - 1];
// }

// void btree_cursor_add_first_step(struct BTreeCursor *cursor) {
//     add_step(&cursor->steps, new_step(cursor->pager, cursor->root_page));
// }

// struct TreeWalker *new_tree_walker(struct Pager *pager, uint32_t table_root_page, struct IndexConstraints *index_constraints) {
//     struct TreeWalker *tree_walker = malloc(sizeof(struct TreeWalker));
//     if (!tree_walker) {
//         fprintf(stderr, "new_tree_walker: Failed to malloc *tree_walker.\n");
//         exit(1);
//     }

//     init_steps_list(&tree_walker->table_cursor.steps);
//     init_steps_list(&tree_walker->index_cursor.steps);

//     btree_cursor_add_first_step(pager, &tree_walker->table_cursor);

//     struct BTreeCursor table_cursor = { .root_page = table_root_page, .pager = pager, .scan_finished = false};
//     struct BTreeCursor index_cursor;
//     enum WalkStrategy strategy;
//     if (index_constraints == NULL) {
//         strategy = WALK_FULL_SCAN;

//         index_cursor.root_page      = 0;
//         index_cursor.pager          = NULL;
//         index_cursor.scan_finished  = true;
//     } else {
//         strategy = WALK_INDEX_SCAN;

//         index_cursor.root_page      = index_constraints->root_page;
//         index_cursor.pager          = pager;
//         index_cursor.scan_finished  = false;

//         btree_cursor_add_first_step(pager, &tree_walker->index_cursor);
//     }

//     tree_walker->strategy               = strategy;
//     tree_walker->table_cursor           = table_cursor;
//     tree_walker->index_cursor           = index_cursor;
//     tree_walker->index_constraints      = index_constraints;


//     return tree_walker;
// }

// void interior_index_step(struct BTreeCursor *cursor, struct StepData *step, struct IndexConstraints *constraints) {
//     struct Row interior_index_row;


//     // Get frequently used pointers
//     struct Pager *pager = cursor->pager;
//     struct PageHeader *page_header = step->page_header;

//     int lo = step->search_start;
//     int hi = step->page_header->number_of_cells - 1;
//     int mid = lo + (hi + lo) / 2;
//     int result = -1;

//     uint16_t cell_offset;

//     while (lo <= hi) {
//         mid = lo + (hi - lo) / 2;
//         cell_offset = step->cell_pointer_array[mid];
//         read_row_from_cell(pager, &interior_index_row, page_header, cell_offset, false);
        
//         assert(interior_index_row.column_count > 0);

//         // Evaluate index

//     }
// }

// int compare_index_constraints(struct IndexConstraints *constraints, struct Row *row) {
//     // Compares index contraints to row values
//     // Returns 1 if we need to search right
//     // Returns -1 if we need to search left
//     // Returns 0 if matching

//     for (int i = 0; i < constraints->key_count; i++) {
//         struct Value constraint_value   = get_predicate_value(constraints->keys[i]);
//         struct Value row_value          = row->values[i];

//         int cmp_result = compare_values(&constraint_value, &row_value);
//         if (cmp_result == 0) { // Values are equal, must use next column
//             continue;
//         }
//         return cmp_result;
//     }

//     return 0;
// }

// bool take_step(struct BTreeCursor *cursor, struct IndexConstraints *constraints) {
//     struct StepData *next_step = get_last_step(cursor);
//     if (!next_step) {
//         return false;
//     }

//     switch (next_step->page_header->page_type) {

//         case PAGE_INTERIOR_INDEX:
//             interior_index_step(cursor, next_step, constraints);
//             break;

//         case PAGE_LEAF_INDEX:
//             break;

//         case PAGE_INTERIOR_TABLE:
//             break;

//         case PAGE_LEAF_TABLE:
//             break;

//         default:
//             fprintf(stderr, "Unknown page tpye: %d.\n", next_step->page_header->page_type);
//             exit(1);
//     }
// }
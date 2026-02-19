#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "pager.h"
#include "data_parsing/page_parsing.h"
#include "data_parsing/cell_parsing.h"
#include "data_parsing/record_parsing.h"
#include "sql_utils.h"
#include "planning/plan.h"

int command_db_info(struct Pager *pager) {

    printf("database page size: %u\n", pager->page_size);
    printf("number of tables: %u\n", pager->schema_page_header->number_of_cells);

    return 0;
}

int command_tables(struct Pager *pager) {

    // @TODO: number of tables should be obtained by couting
    // cells on leaf pages
    
    uint16_t number_of_tables = pager->schema_page_header->number_of_cells;
    uint16_t *offsets = read_cell_pointer_array(pager, pager->schema_page_header);

    // Read the cells
    for (int i = 0; i < number_of_tables; i++) {
        struct Cell cell;
        struct SchemaRecord schema_record;

        read_cell_and_schema_record(
            pager,
            pager->schema_page_header,
            1,
            &cell,
            offsets[i],
            &schema_record
        );

        printf("%s ", schema_record.body.table_name);
        free_schema_record(&schema_record);
    }

    free(offsets);

    return 0;
}

int command_sql(struct Pager *pager, const char *command) {
    fprintf(stderr, "Parsing SQL statement\n");

    struct Parser parser;
    
    struct TriePool *reserved_words_pool = init_reserved_words();

    struct SelectStatement *select_stmt = parse(&parser, command, reserved_words_pool);
    print_select_statement_to_stderr(select_stmt, 4);

    struct Plan *plan = build_plan(pager, select_stmt);
    plan_execute(pager, plan);

    return 0;
}
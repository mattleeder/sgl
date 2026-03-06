#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "sql_utils.h"
#include "comparisons.h"
#include "memory.h"

#define HASH_MAP_MIN_CAPACITY (8)

void print_expr_star_to_stderr(int padding) {
    fprintf(stderr, "%*sStar\n", padding, "");
}

void print_expr_column_to_stderr(struct ExprColumn *column, int padding) {
    assert(column != NULL);

    fprintf(stderr, "%*sColumn\n", padding, "");
    fprintf(stderr, "%*sName: %.*s\n", padding + 4, "", (int)column->name.len, column->name.start);
}

void print_expr_function_to_stderr(struct ExprFunction *function, int padding) {
    assert(function != NULL);

    fprintf(stderr, "%*sFunction\n", padding, "");
    fprintf(stderr, "%*sName: %s\n", padding + 4, "", function->name);
    print_expression_list_to_stderr(function->args, padding + 4);
}

void print_expr_binary_to_stderr(struct ExprBinary *binary, int padding) {
    assert(binary != NULL);

    fprintf(stderr, "%*sBinary\n", padding, "");
    fprintf(stderr, "%*sOp: %d\n", padding + 4, "", binary->op);
    fprintf(stderr, "%*sLeft\n", padding + 4, "");
    print_expression_to_stderr(binary->left, padding + 8);
    fprintf(stderr, "%*sRight\n", padding + 4, "");
    print_expression_to_stderr(binary->right, padding + 8);
}

void print_expr_string_to_stderr(struct ExprString *string, int padding) {
    assert(string != NULL);

    fprintf(stderr, "%*sString\n", padding, "");
    fprintf(stderr, "%*s%.*s\n", padding + 4, "", (int)string->string.len, string->string.start);
}

void print_expression_to_stderr(struct Expr *expr, int padding) {
    assert(expr != NULL);

    fprintf(stderr, "%*sExpression\n", padding, "");

    switch(expr->type) {
        case EXPR_COLUMN:
            print_expr_column_to_stderr(&expr->column, padding + 4);
            break;

        case EXPR_FUNCTION:
            print_expr_function_to_stderr(&expr->function, padding + 4);
            break;

        case EXPR_STAR:
            print_expr_star_to_stderr(padding + 4);
            break;

        case EXPR_BINARY:
            print_expr_binary_to_stderr(&expr->binary, padding + 4);
            break;

        case EXPR_STRING:
            print_expr_string_to_stderr(&expr->string, padding + 4);
            break;

        default:
            fprintf(stderr, "%.*sPrinting not implemented for expression type %d\n", padding + 4, "", expr->type);
            break;
    }
}

void print_expression_list_to_stderr(struct ExprList *expr_list, int padding) {
    if (expr_list == NULL) {
        fprintf(stderr, "print_expression_list_to_stderr: expr_list is NULL\n");
        exit(1);
    }

    fprintf(stderr, "%*sExpressionList: %zu expressions\n", padding, "", expr_list->count);
    for (size_t i = 0; i < expr_list->count; i++) {
        print_expression_to_stderr(&expr_list->data[i], padding + 4);
    }
}

void print_from_statement_to_stderr(char* from_table, int padding) {
    if (from_table == NULL) {
        fprintf(stderr, "print_from_statement_to_stderr: from_table is NULL.\n");
        exit(1);
    }

    fprintf(stderr, "%*sFrom: %s\n", padding, "", from_table);
}

void print_select_statement_to_stderr(struct SelectStatement *stmt, int padding) {
    if(stmt == NULL) {
        fprintf(stderr, "print_select_statement_to_stderr: stmt is NULL\n");
        exit(1);
    }

    if (padding < 0) {
        fprintf(stderr, "print_select_statement_to_stderr: negative padding\n");
        exit(1);
    }

    fprintf(stderr, "\n\nPrinting Select Statement\n\n");
    fprintf(stderr, "%*sSelectStatement\n", padding, "");
    
    if (stmt->select_list == NULL) {
        fprintf(stderr, "stmt->select_list is NULL\n");
        exit(1);
    }
    print_expression_list_to_stderr(stmt->select_list, padding + 4);
    print_from_statement_to_stderr(stmt->from_table, padding + 4);    

    if (stmt->where_list != NULL) {
        fprintf(stderr, "%*sWhere\n", padding, "");
        print_expression_list_to_stderr(stmt->where_list, padding + 4);
    }

    fprintf(stderr, "\n\nPrinting Complete\n\n");
}

void print_binary_expr_list_to_stderr(struct BinaryExprList *expr_list, int padding) {
    assert(expr_list != NULL);
    struct ExprBinary binary;
    for (size_t i = 0; i < expr_list->count; i++) {
        binary = expr_list->data[i];
        print_expr_binary_to_stderr(&binary, padding + 4);
    }
}

void get_column_from_expression(struct Expr *expr, struct HashMap *columns) {
    switch(expr->type) {
        case EXPR_INTEGER:
        case EXPR_STRING:
        case EXPR_FUNCTION:
            break;

        case EXPR_COLUMN: {
            struct Column column = { .index = 0, .name = {.start = expr->column.name.start, .len = expr->column.name.len } };
            bool set_value = true;
            hash_map_column_to_bool_set(columns, &column, &set_value);
            break;
        }

        case EXPR_BINARY:
            get_column_from_expression(expr->binary.left, columns);
            get_column_from_expression(expr->binary.right, columns);
            break;

        case EXPR_UNARY:
            get_column_from_expression(expr->unary.right, columns);
            break;

        default:
            fprintf(stderr, "get_column_from_expression: unknown expr->type %d\n", expr->type);
            exit(1);
    }
}

struct HashMap *get_columns_from_expression_list(struct ExprList *expr_list) {
    struct HashMap *columns = hash_map_column_to_bool_new(
        HASH_MAP_MIN_CAPACITY,
        0.75,
        hash_column_ptr,
        equals_column_ptr
    );

    struct Expr *expr;
    for (size_t i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        get_column_from_expression(expr, columns);
    }

    return columns;
}

struct IndexComparisonArray *get_index_comparisons(struct ExprList *expr_list) {
    struct IndexComparisonArray *array = vector_index_comparison_array_new();

    struct Expr *expr;
    for (size_t i = 0; i < expr_list->count; i++) {
        expr = &expr_list->data[i];
        
        if (expr->type != EXPR_BINARY) {
            continue;
        }

        struct HashMap *column_to_bool_hash_map = hash_map_column_to_bool_new(
            HASH_MAP_MIN_CAPACITY,
            0.75,
            hash_column_ptr,
            equals_column_ptr
        );

        get_column_from_expression(expr, column_to_bool_hash_map);
        if (column_to_bool_hash_map->element_count == 0) {
            hash_map_column_to_bool_free(column_to_bool_hash_map);
            continue;
        }

        size_t number_of_columns;
        struct Column **column_keys = hash_map_column_to_bool_get_keys_alloc(column_to_bool_hash_map, &number_of_columns);

        struct Columns *columns = vector_columns_new();
        for (size_t i = 0; i < number_of_columns; i++) {
            vector_columns_push(columns, *column_keys[i]);
        }
        

        struct IndexComparison comparison = { .binary = expr->binary, .columns = columns };
        vector_index_comparison_array_push(array, comparison);

        hash_map_column_to_bool_free(column_to_bool_hash_map);
    }

    return array;
}



////
void print_new_select_statement_to_stderr(struct SelectStatementNew *stmt, int padding);
void print_join_clause_to_stderr(struct JoinClause *join, int padding);

void print_us_with_prefix_to_stderr(struct UnterminatedString *string, char *prefix, int padding) {
    if (!string) {
        return;
    }
    fprintf(stderr, "%*s%s: %.*s\n", padding, "", prefix, (int)string->len, string->start);
}

void print_new_expr_to_stderr(struct NewExpr *expr, int padding) {
    if (!expr) {
        return;
    }

    fprintf(stderr, "%*sExpression\n", padding, "");
    fprintf(stderr, "%*sPrinting Not Implemented\n", padding + 4, "");

    // @TODO: implement
}

void print_compound_operator_type_to_stderr(enum CompoundOperatorType type, int padding) {
    switch (type) {
        case COMPOUND_OPERATOR_BASE:
            fprintf(stderr, "%*sOperator: BASE\n", padding + 4, "");
            break;

        case COMPOUND_OPERATOR_UNION:
            fprintf(stderr, "%*sOperator: UNION\n", padding + 4, "");
            break;

        case COMPOUND_OPERATOR_UNION_ALL:
            fprintf(stderr, "%*sOperator: UNION ALL\n", padding + 4, "");
            break;

        case COMPOUND_OPERATOR_INTERSECT:
            fprintf(stderr, "%*sOperator: INTERSECT\n", padding + 4, "");
            break;

        case COMPOUND_OPERATOR_EXCEPT:
            fprintf(stderr, "%*sOperator: EXCEPT\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sOperator: UNKNOWN %d\n", padding + 4, "", type);
            exit(1);
    }
}

void print_distinct_to_stderr(bool distinct, int padding) {
    if (distinct) {
        fprintf(stderr, "%*sDinstinct: True\n", padding, "");
    } else {
        fprintf(stderr, "%*sDinstinct: False\n", padding, "");
    }
}

void print_us_to_stderr(struct UnterminatedString *string, int padding) {
    if (!string) {
        return;
    }

    fprintf(stderr, "%*sAlias: %.*s\n", padding, "", (int)string->len, string->start);
}

void print_result_column_to_stderr(struct ResultColumn *column, int padding) {
    fprintf(stderr, "%*sResult Column\n", padding, "");

    switch (column->type) {
        case RC_EXPR:
            fprintf(stderr, "%*sType: Expression\n", padding + 4, "");
            print_new_expr_to_stderr(column->expr.expr, padding + 4);
            print_us_with_prefix_to_stderr(column->expr.alias, "Alias: ", padding + 4);
            break;

        case RC_TABLE_ALL:
            fprintf(stderr, "%*sType: Table All\n", padding + 4, "");
            print_us_with_prefix_to_stderr(&column->table_all.table_name, "Table Name: ", padding + 4);
            break;

        case RC_ALL:
            fprintf(stderr, "%*sType: All\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sType: Unknown\n", padding + 4, "");
            exit(1);
    }
}

void print_result_column_ptr_list_to_stderr(struct ResultColumnPtrList *columns, int padding) {
    if(!columns) {
        return;
    }

    fprintf(stderr, "%*sResult Columns\n", padding, "");

    for (size_t i = 0; i < columns->count; i++) {
        print_result_column_to_stderr(columns->data[i], padding + 4);
    }
}

void print_table_name_to_stderr(struct TableName *table_name, int padding) {
    if (!table_name) {
        return;
    }

    fprintf(stderr, "%*sTable Or Subquery\n", padding, "");

    switch (table_name->index_mode) {
        case TABLE_INDEX_AUTO:
            fprintf(stderr, "%*sIndex Mode: Index Auto\n", padding + 4, "");
            print_us_with_prefix_to_stderr(&table_name->table_name, "Table Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->schema_name, "Schema Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->index_name, "Index Name: ", padding + 4);
            break;

        case TABLE_INDEX_NAMED:
            fprintf(stderr, "%*sIndex Mode: Index Named\n", padding + 4, "");
            print_us_with_prefix_to_stderr(&table_name->table_name, "Table Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->schema_name, "Schema Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->index_name, "Index Name: ", padding + 4);
            break;

        case TABLE_INDEX_NONE:
            fprintf(stderr, "%*sIndex Mode: Index None\n", padding + 4, "");
            print_us_with_prefix_to_stderr(&table_name->table_name, "Table Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->schema_name, "Schema Name: ", padding + 4);
            print_us_with_prefix_to_stderr(table_name->index_name, "Index Name: ", padding + 4);
            break;

        default:
            fprintf(stderr, "%*sIndex Mode: Unknown\n", padding + 4, "");
            exit(1);
    }
}


void print_new_expr_ptr_list_to_stderr(struct NewExprPtrList *list, int padding) {
    if (!list) {
        return;
    }
    
    fprintf(stderr, "%*sExpression List\n", padding, "");

    for (size_t i = 0; i < list->count; i++) {
        
    }
}

void print_table_function_to_stderr(struct TableFunction *table_function, int padding) {
    if (!table_function) {
        return;
    }

    fprintf(stderr, "%*sTable Function\n", padding + 4, "");
    print_us_with_prefix_to_stderr(&table_function->function_name, "Function Name: ", padding + 4);
    print_new_expr_ptr_list_to_stderr(table_function->args, padding + 4);
    print_us_with_prefix_to_stderr(table_function->schema_name, "Schema Name: ", padding + 4);
}

void print_table_or_subquery_to_stderr(struct TableOrSubquery *tos, int padding) {
    if (!tos) {
        return;
    }

    fprintf(stderr, "%*sTable Or Subquery\n", padding, "");

    switch(tos->type) {
        case TOS_TABLE_NAME:
            fprintf(stderr, "%*sType: Table Name\n", padding + 4, "");
            print_us_with_prefix_to_stderr(tos->alias, "Alias: ", padding + 4);
            print_table_name_to_stderr(tos->table_name, padding + 4);
            break;

        case TOS_TABLE_FUNCTION:
            fprintf(stderr, "%*sType: Table Function\n", padding + 4, "");
            print_us_with_prefix_to_stderr(tos->alias, "Alias: ", padding + 4);
            print_table_function_to_stderr(tos->table_function, padding + 4);
            break;

        case TOS_SUBQUERY:
            fprintf(stderr, "%*sType: Subquery\n", padding + 4, "");
            print_us_with_prefix_to_stderr(tos->alias, "Alias: ", padding + 4);
            print_new_select_statement_to_stderr(tos->subquery, padding + 4);
            break;

        default:
            fprintf(stderr, "%*sType: Unknown\n", padding + 4, "");
            exit(1);
    }
}

void print_table_expression_to_stderr(struct TableExpression *table_expr, int padding) {
    if (!table_expr) {
        return;
    }

    fprintf(stderr, "%*sTable Expression\n", padding, "");

    switch (table_expr->type) {
        case TE_SIMPLE:
            fprintf(stderr, "%*sType: Simple\n", padding + 4, "");
            print_table_or_subquery_to_stderr(table_expr->simple, padding + 4);
            break;

        case TE_JOIN:
            fprintf(stderr, "%*sType: Join Clause\n", padding + 4, "");
            print_join_clause_to_stderr(table_expr->join, padding + 4);
            break;

        default:
            fprintf(stderr, "%*sType: Unknown\n", padding + 4, "");
            exit(1);
    }
}

void print_unterminated_string_list_to_stderr(struct UnterminatedStringList *list, int padding) {
    if (!list) {
        return;
    }

    fprintf(stderr, "%*sUnterminated String List\n", padding, "");

    for (size_t i = 0; i < list->count; i++) {
        print_us_with_prefix_to_stderr(&list->data[i], "Column Name: ", padding + 4);
    }
}

void print_join_constraint_to_stderr(struct JoinConstraint *constraint, int padding) {
    if (!constraint) {
        return;
    }

    fprintf(stderr, "%*sJoin Constaint\n", padding, "");

    switch (constraint->type) {
        case JOIN_CONSTRAINT_ON:
            fprintf(stderr, "%*sConstraint Type: ON\n", padding + 4, "");
            print_new_expr_to_stderr(constraint->expr, padding + 4);
            break;

        case JOIN_CONSTRAINT_USING:
            fprintf(stderr, "%*sConstraint Type: USING\n", padding + 4, "");
            print_unterminated_string_list_to_stderr(constraint->column_names, padding + 4);
            break;

        default:
            fprintf(stderr, "%*sConstraint Type: Unknown\n", padding + 4, "");
            exit(1);
    }
}

void print_join_data_to_stderr(struct JoinData *data, int padding) {
    if (!data) {
        return;
    }

    fprintf(stderr, "%*sJoin Data\n", padding, "");

    if (data->natural) {
        fprintf(stderr, "%*sNatural: True\n", padding + 4, "");
    } else {
        fprintf(stderr, "%*sNatural: True\n", padding + 4, "");
    }

    switch (data->join_operator) {
        case JO_CROSS:
            fprintf(stderr, "%*sJoin Operator: Cross\n", padding + 4, "");
            break;

        case JO_INNER:
            fprintf(stderr, "%*sJoin Operator: Inner\n", padding + 4, "");
            break;

        case JO_LEFT_OUTER:
            fprintf(stderr, "%*sJoin Operator: Left Outer\n", padding + 4, "");
            break;

        case JO_RIGHT_OUTER:
            fprintf(stderr, "%*sJoin Operator: Right Outer\n", padding + 4, "");
            break;

        case JO_FULL_OUTER:
            fprintf(stderr, "%*sJoin Operator: Full Outer\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sJoin Operator: Unknown\n", padding + 4, "");
            exit(1);
    }

    print_table_expression_to_stderr(data->right, padding + 4);
    print_join_constraint_to_stderr(data->constraint, padding + 4);
}

void print_join_data_ptr_list_to_stderr(struct JoinDataPtrList *list, int padding) {
    if (!list) {
        return;
    }

    fprintf(stderr, "%*sJoin Data List\n", padding, "");
    for (size_t i = 0; i < list->count; i++) {
        print_join_data_to_stderr(list->data[i], padding + 4);
    }
}

void print_join_clause_to_stderr(struct JoinClause *join, int padding) {
    if (!join) {
        return;
    }

    fprintf(stderr, "%*sJoin Clause\n", padding, "");

    print_table_expression_to_stderr(join->left, padding + 4);
    print_join_data_ptr_list_to_stderr(join->joins, padding + 4);
}

void print_from_clause_to_stderr(struct FromClause *from, int padding) {
    if (!from) {
        return;
    }

    fprintf(stderr, "%*sFrom Clause\n", padding, "");

    print_join_clause_to_stderr(from->tables, padding + 4);
}

void print_where_clause_to_stderr(struct WhereClause *where, int padding) {
    if (!where) {
        return;
    }

    fprintf(stderr, "%*sWhere Clause\n", padding, "");

    print_new_expr_to_stderr(where->expr, padding + 4);
}

void print_group_by_clause_to_stderr(struct GroupByClause *group_by, int padding) {
    if (!group_by) {
        return;
    }

    fprintf(stderr, "%*sGroup By Clause\n", padding, "");

    print_new_expr_ptr_list_to_stderr(group_by->expr_ptr_list, padding + 4);
}

void print_having_clause_to_stderr(struct HavingClause *having, int padding) {
    if (!having) {
        return;
    }

    fprintf(stderr, "%*sHaving Clause\n", padding, "");

    print_new_expr_to_stderr(having->expr, padding + 4);
}

void print_order_by_clause_to_stderr(struct OrderByClause *order_by, int padding) {
    if (!order_by) {
        return;
    }

    fprintf(stderr, "%*sOrder By: \n", padding, "");

    switch (order_by->sort_type) {
        case SORT_ASC:
            fprintf(stderr, "%*sSort: ASC\n", padding + 4, "");
            break;

        case SORT_DESC:
            fprintf(stderr, "%*sSort: DESC\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sSort: Unknown\n", padding + 4, "");
            exit(1);
    }

    switch (order_by->collate_type) {

        case COLLATE_NONE:
            fprintf(stderr, "%*sCollate: None\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sCollate: Unknown\n", padding + 4, "");
            exit(1);

    }

    switch (order_by->nulls_position) {
        case NULLS_NONE:
            fprintf(stderr, "%*sNulls: None\n", padding + 4, "");
            break;

        case NULLS_FIRST:
            fprintf(stderr, "%*sNulls: First\n", padding + 4, "");
            break;

        case NULLS_LAST:
            fprintf(stderr, "%*sNulls: Last\n", padding + 4, "");
            break;

        default:
            fprintf(stderr, "%*sNulls: Unknown\n", padding + 4, "");
            exit(1);
    }

    print_new_expr_to_stderr(order_by->expr, padding + 4);
}

void print_order_by_clause_ptr_list_to_stderr(struct OrderByClausePtrList *list, int padding) {
    if (!list) {
        return;
    }

    fprintf(stderr, "%*sOrder By List: \n", padding, "");

    for (size_t i = 0; i < list->count; i++) {
        print_order_by_clause_to_stderr(list->data[i], padding + 4);
    }
}

void print_frame_bound_to_stderr(struct FrameBound *frame_bound, int padding) {
    if (!frame_bound) {
        return;
    }

    fprintf(stderr, "%*sFrame Bound: \n", padding, "");

    switch (frame_bound->type) {
    case FB_UNBOUNDED_PRECEDING:
        fprintf(stderr, "%*sFrame Bound Type: Unbounded Preceding\n", padding + 4, "");
        break;
    case FB_CURRENT_ROW:
        fprintf(stderr, "%*sFrame Bound Type: Current Row\n", padding + 4, "");
        break;
    case FB_N_PRECEDING:
        fprintf(stderr, "%*sFrame Bound Type: N Preceding\n", padding + 4, "");
        break;
    case FB_N_FOLLOWING:
        fprintf(stderr, "%*sFrame Bound Type: N Following\n", padding + 4, "");
        break;
    case FB_UNBOUNDED_FOLLOWING:
        fprintf(stderr, "%*sFrame Bound Type: Unbounded Following\n", padding + 4, "");
        break;

    default:
        fprintf(stderr, "%*sFrame Bound Type: Unknown\n", padding + 4, "");
        exit(1);
    }

    print_new_expr_to_stderr(frame_bound->offset, padding + 4);
}

void print_frame_spec_to_stderr(struct FrameSpec *frame_spec, int padding) {
    if (!frame_spec) {
        return;
    }

    fprintf(stderr, "%*sFrame Spec: \n", padding, "");

    switch(frame_spec->type) {
        case FRAME_RANGE:
            fprintf(stderr, "%*sFrame Type: Range\n", padding + 4, "");
            break;

        case FRAME_ROWS:
            fprintf(stderr, "%*sFrame Type: Rows\n", padding + 4, "");
            break;

        case FRAME_GROUPS:
            fprintf(stderr, "%*sFrame Type: Groups\n", padding + 4, "");
            break;
        
        default:
            fprintf(stderr, "%*sFrame Type: Unknown\n", padding + 4, "");
            exit(1);
    }

    fprintf(stderr, "%*sFrame Start: \n", padding + 4, "");
    print_frame_bound_to_stderr(&frame_spec->start, padding + 8);

    fprintf(stderr, "%*sFrame End: \n", padding + 4, "");
    print_frame_bound_to_stderr(&frame_spec->end, padding + 8);

    switch(frame_spec->exclude) {
        case EXCLUDE_NO_OTHERS:
            fprintf(stderr, "%*sFrame Exclude: No Others\n", padding + 4, "");
            break;

        case EXCLUDE_CURRENT_ROW:
            fprintf(stderr, "%*sFrame Exclude: Current Row\n", padding + 4, "");
            break;

        case EXCLUDE_GROUP:
            fprintf(stderr, "%*sFrame Exclude: Group\n", padding + 4, "");
            break;

        case EXCLUDE_TIES:
            fprintf(stderr, "%*sFrame Exclude: Ties\n", padding + 4, "");
            break;
        
        default:
            fprintf(stderr, "%*sFrame Exclude: Unknown\n", padding + 4, "");
            exit(1);
    }
}

void print_window_definition_to_stderr(struct WindowDefinition *definition, int padding) {
    if (!definition) {
        return;
    }

    fprintf(stderr, "%*sWindow Definition\n", padding, "");

    print_us_with_prefix_to_stderr(definition->base_window, "Base Window: ", padding + 4);

    fprintf(stderr, "%*sPartition By: \n", padding + 4, "");
    print_new_expr_ptr_list_to_stderr(definition->partition_by, padding + 8);

    print_order_by_clause_ptr_list_to_stderr(definition->order_by, padding + 4);

    print_frame_spec_to_stderr(definition->frame_spec, padding + 4);
}

void print_window_data_to_stderr(struct WindowData *data, int padding) {
    if (!data) {
        return;
    }

    fprintf(stderr, "%*sWindow Data\n", padding, "");
    print_us_with_prefix_to_stderr(&data->name, "Window Name: ", padding + 4);
    print_window_definition_to_stderr(data->definition, padding + 4);
}

void print_window_data_ptr_list_to_stderr(struct WindowDataPtrList *list, int padding) {
    if (!list) {
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        print_window_data_to_stderr(list->data[i], padding + 4);
    }
}

void print_window_clause_to_stderr(struct WindowClause *window, int padding) {
    if (!window) {
        return;
    }

    fprintf(stderr, "%*sWindow Clause\n", padding, "");

    print_window_data_ptr_list_to_stderr(window->window_list, padding + 4);
}

void print_select_core_to_stderr(struct SelectCore *core, int padding) {
    if (!core) {
        return;
    }

    fprintf(stderr, "%*sSelect Core\n", padding, "");

    switch (core->type) {
        case SC_VALUES:
            break;

        case SC_SELECT:
            print_distinct_to_stderr(core->select.distinct, padding + 4);
            print_result_column_ptr_list_to_stderr(core->select.result_columns, padding + 4);
            print_from_clause_to_stderr(core->select.from, padding + 4);
            print_where_clause_to_stderr(core->select.where, padding + 4);
            print_group_by_clause_to_stderr(core->select.group_by, padding + 4);
            print_having_clause_to_stderr(core->select.having, padding + 4);
            print_window_clause_to_stderr(core->select.window, padding + 4);
            break;         
    }
}

void print_select_core_data_to_stderr(struct SelectCoreData *data, int padding) {
    if (!data) {
        return;
    }

    fprintf(stderr, "%*sSelectCoreData\n", padding, "");

    print_compound_operator_type_to_stderr(data->type, padding + 4);
    print_select_core_to_stderr(data->core, padding + 4);
}

void print_select_core_data_list_to_stderr(struct SelectCoreDataPtrList *cores, int padding) {
    if (!cores) {
        return;
    }

    fprintf(stderr, "%*sSelectCoreDataPtrList: %zu expressions\n", padding, "", cores->count);

    for (size_t i = 0; i < cores->count; i++) {
        print_select_core_data_to_stderr(cores->data[i], padding + 4);
    }
}

void print_limit_clause_to_stderr(struct LimitClause *limit, int padding) {
    if (!limit) {
        return;
    }

    fprintf(stderr, "%*sLimit Clause:\n", padding, "");
    
    fprintf(stderr, "%*sLimit: \n", padding, "");
    print_new_expr_to_stderr(limit->limit, padding + 8);

    fprintf(stderr, "%*sOffset: \n", padding, "");
    print_new_expr_to_stderr(limit->offset, padding + 8);
}

void print_new_select_statement_to_stderr(struct SelectStatementNew *stmt, int padding) {
    if(!stmt) {
        return;
    }

    assert(padding > 0);

    fprintf(stderr, "\n\nPrinting New Select Statement\n\n");
    fprintf(stderr, "%*sSelectStatementNew\n", padding, "");

    print_select_core_data_list_to_stderr(stmt->cores, padding + 4);

    print_order_by_clause_ptr_list_to_stderr(stmt->order, padding + 4);

    print_limit_clause_to_stderr(stmt->limit, padding + 4);

    fprintf(stderr, "\n\nPrinting Complete\n\n");
}
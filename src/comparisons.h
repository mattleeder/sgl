#ifndef sql_comparisons
#define sql_comparisons

#include "data_parsing/row_parsing.h"

struct Value get_predicate_value(struct ExprBinary *predicate);
bool compare_index_predicate(struct ExprBinary *predicate, struct Value *column_value, struct Value *predicate_value);
int compare_values(struct Value *left, struct Value *right);

enum CMP_VALUE_TYPE {
    CMP_COLUMN,
    CMP_VALUE
};

struct CmpColumnData {
    uint64_t idx;
};

struct IndexComparison {
    struct ExprBinary   binary;
    struct Columns      *columns;
};

DEFINE_VECTOR(struct IndexComparison, IndexComparisonArray, index_comparison_array)

#endif
#ifndef sql_comparisons
#define sql_comparisons

struct Value get_predicate_value(struct ExprBinary *predicate);
bool compare_index_predicate(struct ExprBinary *predicate, struct Value *column_value, struct Value *predicate_value);
int compare_values(struct Value *left, struct Value *right);

#endif